// Native setup: the install / uninstall work. A port of tools/vg_setup.py (which stays the reference: tests compare the
// two), without Python so the setup is a small plain program.
//
// Void Gate goes into the game's mod catalogs (base/gameN_voidmod.index + .resources and master.index, the layout
// Void Installer writes too) ON TOP of whatever mods are there: their entries and data are carried over, ours are
// added. The game folder's state before the first install is kept in base/voidgate_backup (state.json lists the
// files, and the hashes of what we wrote, so a later Void Installer run is noticed); uninstall puts exactly that back.
// Our entries are stored uncompressed (size == compressed size; the game's own data has thousands like that), so the
// only codec needed is the inflate (inflate.c) for the text files our strings are merged into.
#include <windows.h>
#include <bcrypt.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

#include "../vg_sig.h"
#include "core.h"
#include "payload.h"

int vg_inflate(const uint8_t *in, size_t inLen, uint8_t *out, size_t outLen);

void (*vg_say)(const char *en, const char *ru);
static char g_err[600];

static void say(const char *en, const char *ru)
{
    if (vg_say)
        vg_say(en, ru);
}

static int fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    return 0;
}

const char *vg_last_error(void) { return g_err; }

// ---------------------------------------------------------------- files
static void jpath(wchar_t *out, const wchar_t *dir, const char *rel)  // dir \ rel (ASCII rel, '/' allowed)
{
    _snwprintf(out, MAX_PATH, L"%ls\\%hs", dir, rel);
    out[MAX_PATH - 1] = 0;
    for (wchar_t *p = out; *p; p++)
        if (*p == L'/')
            *p = L'\\';
}

static int exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static uint8_t *read_all(const wchar_t *p, size_t *n)
{
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    uint8_t *b = malloc((size_t)sz.QuadPart + 1);
    DWORD got = 0;
    size_t done = 0;
    while (b && done < (size_t)sz.QuadPart &&
           ReadFile(h, b + done, (DWORD)min((size_t)sz.QuadPart - done, (size_t)1 << 30), &got, NULL) && got)
        done += got;
    CloseHandle(h);
    if (!b || done != (size_t)sz.QuadPart) {
        free(b);
        return NULL;
    }
    b[done] = 0;
    *n = done;
    return b;
}

static int read_at(const wchar_t *p, uint64_t off, void *buf, uint32_t n)
{
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    LARGE_INTEGER o;
    o.QuadPart = (LONGLONG)off;
    DWORD got = 0;
    int ok = SetFilePointerEx(h, o, NULL, FILE_BEGIN) && ReadFile(h, buf, n, &got, NULL) && got == n;
    CloseHandle(h);
    return ok;
}

static int write_all(const wchar_t *p, const void *b, size_t n)
{
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD put = 0;
    size_t done = 0;
    while (done < n && WriteFile(h, (const uint8_t *)b + done, (DWORD)min(n - done, (size_t)1 << 30), &put, NULL) && put)
        done += put;
    CloseHandle(h);
    return done == n;
}

// ---------------------------------------------------------------- growable byte buffer
typedef struct {
    uint8_t *p;
    size_t n, cap;
} buf_t;

static void put(buf_t *b, const void *d, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 4096;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void put_u32be(buf_t *b, uint32_t v)
{
    uint8_t x[4] = {v >> 24, v >> 16, v >> 8, v};
    put(b, x, 4);
}
static void put_u32le(buf_t *b, uint32_t v) { put(b, &v, 4); }
static void put_lstr(buf_t *b, const char *s)  // u32 LE length + bytes
{
    put_u32le(b, (uint32_t)strlen(s));
    put(b, s, strlen(s));
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// ---------------------------------------------------------------- index files
// "\x05SER", u32 BE body length, 24 zero bytes, body: u32 BE count, entries {u32 BE id; type, name, path as u32 LE
// length + bytes; u64 BE offset; u32 BE size; u32 BE compressed size; 10 bytes (byte 9: data file << 2 | catalog-1)}
typedef struct {
    uint32_t id;
    char *type, *name, *path;
    uint64_t off;
    uint32_t size, zsize;
    uint8_t tail[10];
} ent_t;

typedef struct {
    ent_t *e;
    int n, cap;
    char *arena;
} index_t;

static void index_free(index_t *x)
{
    free(x->e);
    free(x->arena);
    memset(x, 0, sizeof *x);
}

static int read_index(const wchar_t *p, index_t *x)
{
    size_t n;
    uint8_t *d = read_all(p, &n);
    memset(x, 0, sizeof *x);
    if (!d)
        return fail("cannot read %ls", p);
    if (n < 0x24 || memcmp(d, "\x05SER", 4)) {
        free(d);
        return fail("%ls is not an index file", p);
    }
    uint32_t cnt = be32(d + 0x20);
    x->e = calloc(cnt + 64, sizeof(ent_t));
    x->cap = cnt + 64;
    x->arena = malloc(n);
    char *a = x->arena;
    size_t pos = 0x24;
    for (uint32_t i = 0; i < cnt; i++) {
        ent_t *e = &x->e[i];
        if (pos + 4 > n)
            goto bad;
        e->id = be32(d + pos), pos += 4;
        char **f[3] = {&e->type, &e->name, &e->path};
        for (int k = 0; k < 3; k++) {
            if (pos + 4 > n)
                goto bad;
            uint32_t ln = le32(d + pos);
            pos += 4;
            if (pos + ln > n)
                goto bad;
            memcpy(a, d + pos, ln);
            a[ln] = 0;
            *f[k] = a;
            a += ln + 1, pos += ln;
        }
        if (pos + 26 > n)
            goto bad;
        e->off = (uint64_t)be32(d + pos) << 32 | be32(d + pos + 4);
        e->size = be32(d + pos + 8), e->zsize = be32(d + pos + 12);
        memcpy(e->tail, d + pos + 16, 10);
        pos += 26;
    }
    x->n = (int)cnt;
    free(d);
    return 1;
bad:
    free(d);
    index_free(x);
    return fail("%ls is damaged", p);
}

static int write_index(const wchar_t *p, const ent_t *e, int n)
{
    buf_t body = {0};
    put_u32be(&body, (uint32_t)n);
    for (int i = 0; i < n; i++) {
        put_u32be(&body, e[i].id);
        put_lstr(&body, e[i].type);
        put_lstr(&body, e[i].name);
        put_lstr(&body, e[i].path);
        put_u32be(&body, (uint32_t)(e[i].off >> 32));
        put_u32be(&body, (uint32_t)e[i].off);
        put_u32be(&body, e[i].size);
        put_u32be(&body, e[i].zsize);
        put(&body, e[i].tail, 10);
    }
    buf_t f = {0};
    put(&f, "\x05SER", 4);
    put_u32be(&f, (uint32_t)body.n);
    uint8_t zero[24] = {0};
    put(&f, zero, 24);
    put(&f, body.p, body.n);
    int ok = write_all(p, f.p, f.n);
    free(body.p);
    free(f.p);
    return ok ? 1 : fail("cannot write %ls", p);
}

// ---------------------------------------------------------------- a catalog: its current index and data files
static const char *const CONTAINERS[8] = {"", "_001", "_002", "_003", "_004", "_005", "_patch", "_voidmod"};
#define VOIDMOD 7

typedef struct {
    int g;
    const wchar_t *base;
    index_t ix;
    int *head;  // path hash table: first entry with the path
    int *next;  // next entry with the same path
    uint32_t mask;
} cat_t;

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static int cat_find(const cat_t *c, const char *path)  // first entry with this path, -1 if none
{
    for (uint32_t s = fnv(path) & c->mask;; s = (s + 1) & c->mask) {
        int i = c->head[s];
        if (i < 0)
            return -1;
        if (!strcmp(c->ix.e[i].path, path))
            return i;
    }
}

static int cat_open(cat_t *c, const wchar_t *base, int g)
{
    memset(c, 0, sizeof *c);
    c->g = g, c->base = base;
    wchar_t p[MAX_PATH];
    char fn[64];
    snprintf(fn, sizeof fn, "game%d_voidmod.index", g);
    jpath(p, base, fn);
    if (!exists(p)) {
        snprintf(fn, sizeof fn, "game%d_patch.index", g);
        jpath(p, base, fn);
    }
    if (!read_index(p, &c->ix))
        return 0;
    uint32_t sz = 1024;
    while (sz < (uint32_t)c->ix.n * 2)
        sz <<= 1;
    c->mask = sz - 1;
    c->head = malloc(sz * sizeof(int));
    memset(c->head, 0xFF, sz * sizeof(int));
    c->next = malloc((c->ix.n + 1) * sizeof(int));
    int *last = malloc(sz * sizeof(int));
    for (int i = 0; i < c->ix.n; i++) {
        c->next[i] = -1;
        for (uint32_t s = fnv(c->ix.e[i].path) & c->mask;; s = (s + 1) & c->mask) {
            int h = c->head[s];
            if (h < 0) {
                c->head[s] = i, last[s] = i;
                break;
            }
            if (!strcmp(c->ix.e[h].path, c->ix.e[i].path)) {
                c->next[last[s]] = i, last[s] = i;
                break;
            }
        }
    }
    free(last);
    return 1;
}

static void cat_close(cat_t *c)
{
    index_free(&c->ix);
    free(c->head);
    free(c->next);
}

static void container_path(const cat_t *c, int container, wchar_t *out)
{
    char fn[64];
    snprintf(fn, sizeof fn, "game%d%s.resources", c->g, CONTAINERS[container & 7]);
    jpath(out, c->base, fn);
}

static uint8_t *cat_read(const cat_t *c, int i)  // the entry's bytes as stored
{
    const ent_t *e = &c->ix.e[i];
    wchar_t p[MAX_PATH];
    container_path(c, e->tail[9] >> 2, p);
    uint8_t *b = malloc(e->zsize ? e->zsize : 1);
    if (!read_at(p, e->off, b, e->zsize)) {
        free(b);
        fail("cannot read %s from %ls", e->path, p);
        return NULL;
    }
    return b;
}

static uint8_t *cat_data(const cat_t *c, int i)  // the entry's content (inflated when compressed)
{
    const ent_t *e = &c->ix.e[i];
    uint8_t *raw = cat_read(c, i);
    if (!raw || e->zsize == e->size)
        return raw;
    uint8_t *out = malloc(e->size + 1);
    if (vg_inflate(raw, e->zsize, out, e->size)) {
        free(raw);
        free(out);
        fail("cannot unpack %s", e->path);
        return NULL;
    }
    free(raw);
    return out;
}

// ---------------------------------------------------------------- the text files: our strings on top
typedef struct {
    const char *p;
    size_t n;
} line_t;

static int strip_is_brace(const line_t *l)
{
    size_t a = 0, b = l->n;
    while (a < b && strchr(" \t\r\n\v\f", l->p[a]))
        a++;
    while (b > a && strchr(" \t\r\n\v\f", l->p[b - 1]))
        b--;
    return b - a == 1 && l->p[a] == '}';
}

// key of a  <TAB>"key"<TAB>...  line (as the Python reference: \t"([^"]+)"\t at the start), else NULL
static int line_key(const line_t *l, size_t *ks, size_t *kn)
{
    if (l->n < 4 || l->p[0] != '\t' || l->p[1] != '"')
        return 0;
    size_t e = 2;
    while (e < l->n && l->p[e] != '"')
        e++;
    if (e == 2 || e + 1 >= l->n || l->p[e + 1] != '\t')
        return 0;
    *ks = 2, *kn = e - 2;
    return 1;
}

static uint8_t *merge_lang(const uint8_t *text, size_t n, const vg_lang_t *lang, size_t *outN)
{
    int nl = 1;
    for (size_t i = 0; i < n; i++)
        nl += text[i] == '\n';
    line_t *ln = malloc(nl * sizeof(line_t));
    int k = 0;
    size_t s = 0;
    for (size_t i = 0; i <= n; i++)
        if (i == n || text[i] == '\n') {
            ln[k].p = (const char *)text + s, ln[k].n = i - s, k++;
            s = i + 1;
        }
    int close = -1;
    for (int i = 0; i < nl; i++)
        if (strip_is_brace(&ln[i]))
            close = i;
    if (close < 0) {
        free(ln);
        fail("unexpected text file layout");
        return NULL;
    }
    // our lines, built once; a key already there is replaced in place (its last occurrence), the rest goes before
    // the closing brace
    int nkv = lang->count;
    char **mine = malloc(nkv * sizeof(char *));
    int *at = malloc(nkv * sizeof(int));
    for (int j = 0; j < nkv; j++) {
        const vg_kv_t *kv = &VG_KV[lang->first + j];
        size_t m = strlen(kv->key) + strlen(kv->value) + 8;
        mine[j] = malloc(m);
        snprintf(mine[j], m, "\t\"%s\"\t\"%s\"", kv->key, kv->value);
        at[j] = -1;
        for (int i = 0; i < nl; i++) {
            size_t ks, kn;
            if (line_key(&ln[i], &ks, &kn) && kn == strlen(kv->key) && !memcmp(ln[i].p + ks, kv->key, kn))
                at[j] = i;
        }
    }
    buf_t b = {0};
    for (int i = 0; i < nl; i++) {
        if (i == close)
            for (int j = 0; j < nkv; j++)
                if (at[j] < 0) {
                    put(&b, mine[j], strlen(mine[j]));
                    put(&b, "\n", 1);
                }
        int replaced = -1;
        for (int j = 0; j < nkv; j++)
            if (at[j] == i)
                replaced = j;  // (two of our keys on one line cannot happen: keys differ)
        if (replaced >= 0)
            put(&b, mine[replaced], strlen(mine[replaced]));
        else
            put(&b, ln[i].p, ln[i].n);
        if (i < nl - 1)
            put(&b, "\n", 1);
    }
    for (int j = 0; j < nkv; j++)
        free(mine[j]);
    free(mine);
    free(at);
    free(ln);
    *outN = b.n;
    return b.p;
}

// ---------------------------------------------------------------- what goes into the catalogs
typedef struct {
    const char *path;
    uint8_t *data;       // what to store
    uint32_t size, zsize;
    int present;         // bit g: catalog g has the path
    int replace;         // replace it where present (mod files, text files); copies are only added
    const vg_file_t *f;  // NULL for a text file
} item_t;

#define MAX_ITEMS 256

static int plan(cat_t *cats, item_t *items, int *nitems)
{
    int n = 0;
    for (int i = 0; i < VG_NFILES; i++) {
        const vg_file_t *f = &VG_FILES[i];
        item_t *it = &items[n++];
        memset(it, 0, sizeof *it);
        it->path = f->path, it->f = f;
        for (int g = 1; g <= 4; g++)
            if (cat_find(&cats[g - 1], f->path) >= 0)
                it->present |= 1 << g;
        if (f->copy) {
            // a stock resource another catalog needs: its stored bytes, from the catalog that has it
            int needed = 0;
            for (int g = 1; g <= 4; g++)
                needed |= VG_NEW_CATALOGS[g - 1] && !(it->present >> g & 1);
            if (!needed)
                continue;
            int src = -1, ei = -1;
            for (int g = 1; g <= 4 && src < 0; g++)
                if ((ei = cat_find(&cats[g - 1], f->path)) >= 0)
                    src = g - 1;
            if (src < 0)
                return fail("%s is not in this game's data", f->path);
            if (!(it->data = cat_read(&cats[src], ei)))
                return 0;
            it->size = cats[src].ix.e[ei].size, it->zsize = cats[src].ix.e[ei].zsize;
        } else {
            it->replace = it->present != 0;
            it->data = (uint8_t *)vg_payload + f->off;
            it->size = it->zsize = f->size;
        }
    }
    for (int l = 0; l < VG_NLANGS; l++) {
        static char paths[16][64];
        snprintf(paths[l], 64, "strings/%s.lang", VG_LANGS[l].stem);
        item_t *it = &items[n++];
        memset(it, 0, sizeof *it);
        it->path = paths[l], it->replace = 1;
        int src = -1, ei = -1;
        for (int g = 1; g <= 4; g++) {
            int k = cat_find(&cats[g - 1], it->path);
            if (k >= 0) {
                it->present |= 1 << g;
                if (src < 0)
                    src = g - 1, ei = k;
            }
        }
        if (src < 0) {
            n--;
            continue;
        }
        uint8_t *text = cat_data(&cats[src], ei);
        if (!text)
            return 0;
        size_t mn;
        it->data = merge_lang(text, cats[src].ix.e[ei].size, &VG_LANGS[l], &mn);
        free(text);
        if (!it->data)
            return 0;
        it->size = it->zsize = (uint32_t)mn;
    }
    *nitems = n;
    return 1;
}

static int build_catalog(cat_t *c, item_t *items, int nitems, const wchar_t *tmp)
{
    int g = c->g;
    ent_t *e = malloc((c->ix.n + nitems + 1) * sizeof(ent_t));
    memcpy(e, c->ix.e, c->ix.n * sizeof(ent_t));
    int n = c->ix.n;
    // which item each entry gets (replaced paths: every entry with the path)
    int *mine = malloc((c->ix.n + 1) * sizeof(int));
    for (int i = 0; i < c->ix.n; i++)
        mine[i] = -1;
    for (int k = 0; k < nitems; k++)
        if (items[k].replace && (items[k].present >> g & 1))
            for (int i = cat_find(c, items[k].path); i >= 0; i = c->next[i])
                mine[i] = k;
    wchar_t rp[MAX_PATH], ip[MAX_PATH];
    char fn[64];
    snprintf(fn, sizeof fn, "game%d_voidmod.resources", g);
    jpath(rp, tmp, fn);
    snprintf(fn, sizeof fn, "game%d_voidmod.index", g);
    jpath(ip, tmp, fn);
    HANDLE h = CreateFileW(rp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        free(e), free(mine);
        return fail("cannot write %ls", rp);
    }
    uint64_t pos = 0;
    int ok = 1;
#define EMIT(ptr, len)                                                                                                 \
    do {                                                                                                              \
        DWORD w_;                                                                                                     \
        if (ok && (!WriteFile(h, (ptr), (DWORD)(len), &w_, NULL) || w_ != (DWORD)(len)))                              \
            ok = fail("cannot write %ls", rp);                                                                        \
        pos += (len);                                                                                                 \
    } while (0)
    EMIT("\x04SER", 4);
    for (int i = 0; ok && i < c->ix.n; i++) {
        if (mine[i] >= 0) {
            item_t *it = &items[mine[i]];
            e[i].off = pos, e[i].size = it->size, e[i].zsize = it->zsize;
            e[i].tail[9] = (uint8_t)(VOIDMOD << 2 | (g - 1));
            EMIT(it->data, it->zsize);
        } else if ((e[i].tail[9] >> 2) == VOIDMOD) {  // another mod's data: carried over as it is
            uint8_t *raw = cat_read(c, i);
            if (!raw) {
                ok = 0;
                break;
            }
            e[i].off = pos;
            e[i].tail[9] = (uint8_t)(VOIDMOD << 2 | (g - 1));
            EMIT(raw, e[i].zsize);
            free(raw);
        }
    }
    uint32_t next_id = 0;
    for (int i = 0; i < c->ix.n; i++)
        if (e[i].id + 1 > next_id)
            next_id = e[i].id + 1;
    for (int k = 0; ok && k < nitems; k++) {
        item_t *it = &items[k];
        if (!it->f || it->replace || !VG_NEW_CATALOGS[g - 1] || (it->present >> g & 1) || !it->data)
            continue;
        ent_t *x = &e[n++];
        memset(x, 0, sizeof *x);
        x->id = next_id++;
        x->type = (char *)it->f->type, x->name = (char *)it->f->name, x->path = (char *)it->path;
        x->off = pos, x->size = it->size, x->zsize = it->zsize;
        memcpy(x->tail, it->f->tail, 10);
        x->tail[9] = (uint8_t)(VOIDMOD << 2 | (g - 1));
        EMIT(it->data, it->zsize);
    }
#undef EMIT
    CloseHandle(h);
    // the game's loader wants non-decreasing offsets for consecutive entries of one data file
    for (int i = 1; ok && i < n; i++)
        if (e[i - 1].tail[9] == e[i].tail[9] && e[i - 1].off > e[i].off && (e[i].tail[9] >> 2) == VOIDMOD)
            ok = fail("game%d: the index order would break the game's background loader", g);
    if (ok)
        ok = write_index(ip, e, n);
    free(e);
    free(mine);
    return ok;
}

// master.index pointing the game at the voidmod catalogs (the layout Void Installer writes)
static int write_master(const wchar_t *base, const wchar_t *tmp)
{
    wchar_t p[MAX_PATH];
    jpath(p, base, "master.index");
    size_t n;
    uint8_t *cur = read_all(p, &n);
    if (!cur)
        return fail("cannot read %ls", p);
    int has = 0;
    const char *want = "game1_voidmod.index";
    for (size_t i = 0; i + strlen(want) <= n; i++)
        if (!memcmp(cur + i, want, strlen(want)))
            has = 1;
    if (has) {
        free(cur);
        return 1;
    }
    static const uint8_t mark[8] = {0, 0, 0, 1, 0, 0, 0, 2};
    long t = -1;
    for (size_t i = 0; i + 8 <= n; i++)
        if (!memcmp(cur + i, mark, 8))
            t = (long)i;
    if (t < 0) {
        free(cur);
        return fail("unexpected master.index");
    }
    buf_t b = {0};
    put(&b, "\x04SER", 4);
    uint16_t v = 0x200;
    put(&b, &v, 2);
    v = 0x400;
    put(&b, &v, 2);
    char s[64];
    for (int g = 1; g <= 4; g++) {
        snprintf(s, sizeof s, "game%d_voidmod.index", g);
        put_lstr(&b, s);
        snprintf(s, sizeof s, "game%d.resources", g);
        put_lstr(&b, s);
    }
    put_u32be(&b, 7 * 4);
    for (int k = 1; k < 8; k++)
        for (int g = 1; g <= 4; g++) {
            snprintf(s, sizeof s, "game%d%s.resources", g, CONTAINERS[k]);
            put_lstr(&b, s);
            uint8_t c2[2] = {0, (uint8_t)(g - 1)};
            put(&b, c2, 2);
        }
    put(&b, cur + t, n - t);
    jpath(p, tmp, "master.index");
    int ok = write_all(p, b.p, b.n);
    free(cur);
    free(b.p);
    return ok ? 1 : fail("cannot write %ls", p);
}
// ---------------------------------------------------------------- backup and its state (same state.json as the Python
// setup writes, so either can uninstall what the other installed)
static const char *const MOD_FILES[9] = {
    "master.index",           "game1_voidmod.index", "game1_voidmod.resources", "game2_voidmod.index",
    "game2_voidmod.resources", "game3_voidmod.index", "game3_voidmod.resources", "game4_voidmod.index",
    "game4_voidmod.resources"};
#define UNINSTALLER L"VoidGate_Uninstall.exe"
static const char OUR_MARK[] = "Void Gate mod: this build";  // a string in our dinput8.dll

typedef struct {
    char made[40], version[24];
    int files[9];       // backed-up files (index into MOD_FILES)
    int other_dinput8;  // another mod's dinput8.dll was kept
    int wrote_n;
    int wrote_f[9];
    char wrote_h[9][41];  // SHA-1 of what we wrote, as hex
} state_t;

static void base_dir(const wchar_t *game, wchar_t *out) { jpath(out, game, "base"); }
static void backup_dir(const wchar_t *game, wchar_t *out) { jpath(out, game, "base/voidgate_backup"); }

static int sha1_file(const wchar_t *p, char hex[41])
{
    size_t n;
    uint8_t *d = read_all(p, &n);
    if (!d)
        return 0;
    BCRYPT_ALG_HANDLE alg;
    uint8_t h[20];
    int ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0) == 0;
    if (ok) {
        ok = BCryptHash(alg, NULL, 0, d, (ULONG)n, h, 20) == 0;
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    free(d);
    for (int i = 0; ok && i < 20; i++)
        sprintf(hex + i * 2, "%02x", h[i]);
    return ok;
}

static void current_hashes(const wchar_t *game, state_t *s)
{
    wchar_t base[MAX_PATH], p[MAX_PATH];
    base_dir(game, base);
    s->wrote_n = 0;
    for (int i = 0; i < 9; i++) {
        jpath(p, base, MOD_FILES[i]);
        if (exists(p) && sha1_file(p, s->wrote_h[s->wrote_n]))
            s->wrote_f[s->wrote_n++] = i;
    }
}

static int mod_file_index(const char *name, size_t n)
{
    for (int i = 0; i < 9; i++)
        if (strlen(MOD_FILES[i]) == n && !memcmp(MOD_FILES[i], name, n))
            return i;
    return -1;
}

// next "..." string after *pos (no escapes in our values); returns its start, length in *n
static const char *json_str(const char *s, const char **pos, size_t *n)
{
    const char *a = strchr(*pos, '"');
    if (!a)
        return NULL;
    const char *b = strchr(a + 1, '"');
    if (!b)
        return NULL;
    *n = (size_t)(b - a - 1);
    *pos = b + 1;
    (void)s;
    return a + 1;
}

static int load_state(const wchar_t *game, state_t *s)
{
    wchar_t p[MAX_PATH];
    backup_dir(game, p);
    wcscat(p, L"\\state.json");
    size_t n;
    char *d = (char *)read_all(p, &n);
    memset(s, 0, sizeof *s);
    if (!d)
        return 0;
    const char *f = strstr(d, "\"files\"");
    if (f && (f = strchr(f, '['))) {
        const char *end = strchr(f, ']'), *pos = f, *v;
        size_t ln;
        while (end && (v = json_str(d, &pos, &ln)) && v < end) {
            int k = mod_file_index(v, ln);
            if (k >= 0)
                s->files[k] = 1;
        }
    }
    const char *o = strstr(d, "\"other_dinput8\"");
    if (o && (o = strchr(o, ':'))) {
        while (*++o == ' ')
            ;
        s->other_dinput8 = !strncmp(o, "true", 4);
    }
    const char *w = strstr(d, "\"wrote\"");
    if (w && (w = strchr(w, '{'))) {
        const char *end = strchr(w, '}'), *pos = w, *k, *v;
        size_t kn, vn;
        while (end && (k = json_str(d, &pos, &kn)) && k < end && (v = json_str(d, &pos, &vn)) && v < end) {
            int i = mod_file_index(k, kn);
            if (i >= 0 && vn == 40 && s->wrote_n < 9) {
                s->wrote_f[s->wrote_n] = i;
                memcpy(s->wrote_h[s->wrote_n], v, 40);
                s->wrote_h[s->wrote_n++][40] = 0;
            }
        }
    }
    free(d);
    return 1;
}

static int save_state(const wchar_t *game, const state_t *s)
{
    char out[4096];
    int n = snprintf(out, sizeof out, "{\n \"made\": \"%s\",\n \"version\": \"%s\",\n \"files\": [", s->made, s->version);
    int first = 1;
    for (int i = 0; i < 9; i++)
        if (s->files[i]) {
            n += snprintf(out + n, sizeof out - n, "%s\n  \"%s\"", first ? "" : ",", MOD_FILES[i]);
            first = 0;
        }
    n += snprintf(out + n, sizeof out - n, "%s]", first ? "" : "\n ");
    if (s->other_dinput8)
        n += snprintf(out + n, sizeof out - n, ",\n \"other_dinput8\": true");
    if (s->wrote_n) {
        n += snprintf(out + n, sizeof out - n, ",\n \"wrote\": {");
        for (int i = 0; i < s->wrote_n; i++)
            n += snprintf(out + n, sizeof out - n, "%s\n  \"%s\": \"%s\"", i ? "," : "", MOD_FILES[s->wrote_f[i]],
                          s->wrote_h[i]);
        n += snprintf(out + n, sizeof out - n, "\n }");
    }
    n += snprintf(out + n, sizeof out - n, "\n}");
    wchar_t p[MAX_PATH];
    backup_dir(game, p);
    wcscat(p, L"\\state.json");
    return write_all(p, out, (size_t)n) ? 1 : fail("cannot write %ls", p);
}

int vg_installed(const wchar_t *game)
{
    state_t s;
    return game && *game && load_state(game, &s);
}

int vg_changed_since_install(const wchar_t *game)
{
    state_t s, now;
    if (!load_state(game, &s))
        return 0;
    current_hashes(game, &now);
    if (now.wrote_n != s.wrote_n)
        return 1;
    for (int i = 0; i < now.wrote_n; i++) {
        int found = 0;
        for (int j = 0; j < s.wrote_n; j++)
            if (s.wrote_f[j] == now.wrote_f[i] && !strcmp(s.wrote_h[j], now.wrote_h[i]))
                found = 1;
        if (!found)
            return 1;
    }
    return 0;
}

static int ours(const wchar_t *p)
{
    size_t n;
    uint8_t *d = read_all(p, &n);
    int r = 0;
    for (size_t i = 0; d && !r && i + sizeof OUR_MARK - 1 <= n; i++)
        r = !memcmp(d + i, OUR_MARK, sizeof OUR_MARK - 1);
    free(d);
    return r;
}

static int make_backup(const wchar_t *game, state_t *s)
{
    wchar_t b[MAX_PATH], base[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    backup_dir(game, b);
    base_dir(game, base);
    if (!CreateDirectoryW(b, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return fail("cannot create %ls", b);
    memset(s, 0, sizeof *s);
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(s->made, sizeof s->made, "%04d-%02d-%02dT%02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    snprintf(s->version, sizeof s->version, "%s", VG_VERSION);
    for (int i = 0; i < 9; i++) {
        jpath(src, base, MOD_FILES[i]);
        if (!exists(src))
            continue;
        jpath(dst, b, MOD_FILES[i]);
        if (!CopyFileW(src, dst, FALSE))
            return fail("cannot back up %ls", src);
        s->files[i] = 1;
    }
    jpath(src, game, "dinput8.dll");
    if (exists(src) && !ours(src)) {
        jpath(dst, b, "dinput8.dll.other");
        if (!CopyFileW(src, dst, FALSE))
            return fail("cannot back up %ls", src);
        s->other_dinput8 = 1;
    }
    return save_state(game, s);
}

static int running_is(const wchar_t *p)  // this process is that file
{
    wchar_t me[MAX_PATH], a[MAX_PATH], b[MAX_PATH];
    GetModuleFileNameW(NULL, me, MAX_PATH);
    GetFullPathNameW(me, MAX_PATH, a, NULL);
    GetFullPathNameW(p, MAX_PATH, b, NULL);
    return !_wcsicmp(a, b);
}

static void remove_native(const wchar_t *game, const state_t *s)
{
    static const char *const files[] = {"voidgate.dll", "dmod_d2.log", "dmod_cmd.txt", "dmod_reply.txt"};
    wchar_t p[MAX_PATH], b[MAX_PATH];
    for (int i = 0; i < 4; i++) {
        jpath(p, game, files[i]);
        DeleteFileW(p);
    }
    WIN32_FIND_DATAW fd;
    jpath(p, game, "voidgate.live*.dll");
    HANDLE h = FindFirstFileW(p, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            _snwprintf(b, MAX_PATH, L"%ls\\%ls", game, fd.cFileName);
            DeleteFileW(b);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    jpath(p, game, "dinput8.dll");
    if (s->other_dinput8) {
        backup_dir(game, b);
        wcscat(b, L"\\dinput8.dll.other");
        CopyFileW(b, p, FALSE);
    } else if (exists(p) && ours(p)) {
        DeleteFileW(p);
    }
    _snwprintf(p, MAX_PATH, L"%ls\\%ls", game, UNINSTALLER);
    if (exists(p) && !running_is(p))
        DeleteFileW(p);  // (the uninstaller cannot delete itself while it runs: the window tells the player)
}

static int restore_backup(const wchar_t *game, const state_t *s)
{
    wchar_t b[MAX_PATH], base[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    backup_dir(game, b);
    base_dir(game, base);
    int ok = 1;
    for (int i = 0; i < 9; i++) {
        jpath(dst, base, MOD_FILES[i]);
        if (s->files[i]) {
            jpath(src, b, MOD_FILES[i]);
            if (!CopyFileW(src, dst, FALSE))
                ok = fail("cannot restore %ls", dst);
        } else {
            DeleteFileW(dst);
        }
    }
    remove_native(game, s);
    return ok;
}

static void remove_tree(const wchar_t *dir)  // a folder of files (the backup, the build folder)
{
    wchar_t p[MAX_PATH], q[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            _snwprintf(q, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            SetFileAttributesW(q, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(q);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

static void place_uninstaller(const wchar_t *game)
{
    wchar_t me[MAX_PATH], un[MAX_PATH];
    GetModuleFileNameW(NULL, me, MAX_PATH);
    _snwprintf(un, MAX_PATH, L"%ls\\%ls", game, UNINSTALLER);
    if (!running_is(un))
        CopyFileW(me, un, FALSE);
}

static int write_payload(const wchar_t *p, vg_blob_t b)
{
    return write_all(p, vg_payload + b.off, b.size) ? 1 : fail("cannot write %ls (is the game running?)", p);
}

static int build_and_place(const wchar_t *game)
{
    wchar_t base[MAX_PATH], tmp[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    base_dir(game, base);
    jpath(tmp, base, "voidgate_tmp");
    remove_tree(tmp);
    if (!CreateDirectoryW(tmp, NULL))
        return fail("cannot create %ls", tmp);
    cat_t cats[4];
    int opened = 0, ok = 1;
    for (int g = 1; ok && g <= 4; g++)
        if ((ok = cat_open(&cats[g - 1], base, g)))
            opened = g;
    item_t items[MAX_ITEMS];
    int nitems = 0;
    if (ok)
        ok = plan(cats, items, &nitems);
    for (int g = 1; ok && g <= 4; g++)
        ok = build_catalog(&cats[g - 1], items, nitems, tmp);
    for (int k = 0; k < nitems; k++)  // (payload bytes are not ours to free)
        if (!(items[k].f && !items[k].f->copy))
            free(items[k].data);
    for (int g = 1; g <= opened; g++)
        cat_close(&cats[g - 1]);
    if (ok)
        ok = write_master(base, tmp);
    for (int i = 0; ok && i < 9; i++) {
        jpath(src, tmp, MOD_FILES[i]);
        jpath(dst, base, MOD_FILES[i]);
        if (exists(src) && !MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
            ok = fail("cannot replace %ls", dst);
    }
    remove_tree(tmp);
    if (ok) {
        jpath(dst, game, "dinput8.dll");
        ok = write_payload(dst, VG_DINPUT8);
    }
    if (ok) {
        jpath(dst, game, "voidgate.dll");
        ok = write_payload(dst, VG_MODULE);
    }
    if (ok)
        place_uninstaller(game);
    return ok;
}

int vg_install(const wchar_t *game)
{
    if (vg_game_running()) {
        say("Close Dishonored 2 first.", "Сначала закройте Dishonored 2.");
        return fail("the game is running");
    }
    state_t s;
    wchar_t b[MAX_PATH];
    backup_dir(game, b);
    if (load_state(game, &s)) {
        if (vg_changed_since_install(game)) {
            // other mods were (re)installed over ours: build on what is there now, keep the original backup
            say("The mod catalogs changed since Void Gate was installed (other mods?): adding Void Gate to them.",
                "Каталоги модов изменились после установки Врат Бездны (другие моды?): добавляю Врата к ним.");
        } else {
            say("Void Gate is already installed: reinstalling.", "Врата Бездны уже установлены: переустанавливаю.");
            if (!restore_backup(game, &s))
                return 0;
        }
    } else if (!make_backup(game, &s)) {
        remove_tree(b);
        return 0;
    }
    say("Adding Void Gate to the game's mod catalogs...", "Добавляю Врата Бездны в каталоги модов игры...");
    if (!build_and_place(game)) {
        char why[600];
        snprintf(why, sizeof why, "%s", g_err);
        say("Install failed: putting the game folder back as it was.",
            "Установка не удалась: возвращаю папку игры как было.");
        restore_backup(game, &s);
        remove_tree(b);
        snprintf(g_err, sizeof g_err, "%s", why);
        return 0;
    }
    current_hashes(game, &s);
    if (!save_state(game, &s))
        return 0;
    say("Installed. In the game: journal -> Powers -> Void Gate (buy it with runes).",
        "Установлено. В игре: журнал -> Умения -> Врата Бездны (покупаются за руны).");
    return 1;
}

int vg_uninstall(const wchar_t *game)
{
    if (vg_game_running()) {
        say("Close Dishonored 2 first.", "Сначала закройте Dishonored 2.");
        return fail("the game is running");
    }
    state_t s;
    if (!load_state(game, &s)) {
        say("Void Gate is not installed here.", "Врата Бездны здесь не установлены.");
        return fail("not installed");
    }
    say("Putting the game folder back as it was before Void Gate...", "Возвращаю папку игры как было до Врат Бездны...");
    if (!restore_backup(game, &s))
        return 0;
    wchar_t b[MAX_PATH];
    backup_dir(game, b);
    remove_tree(b);
    say("Removed: the game folder is as it was before Void Gate.",
        "Удалено: папка игры такая же, как до установки Врат Бездны.");
    return 1;
}

// ---------------------------------------------------------------- the game folder
int vg_is_game(const wchar_t *d)
{
    wchar_t p[MAX_PATH];
    if (!d || !*d)
        return 0;
    jpath(p, d, "Dishonored2.exe");
    if (!exists(p))
        return 0;
    jpath(p, d, "base/game1_patch.index");
    return exists(p);
}

int vg_game_running(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = {sizeof pe};
    int found = 0;
    if (snap != INVALID_HANDLE_VALUE) {
        for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
            found = !_wcsicmp(pe.szExeFile, L"Dishonored2.exe");
        CloseHandle(snap);
    }
    return found;
}

int vg_writable(const wchar_t *game)
{
    wchar_t p[MAX_PATH];
    jpath(p, game, "base/voidgate_write_test.tmp");
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CloseHandle(h);
    return 1;
}

static int try_dir(const wchar_t *d, wchar_t *out)
{
    if (!vg_is_game(d))
        return 0;
    GetFullPathNameW(d, MAX_PATH, out, NULL);
    size_t n = wcslen(out);
    while (n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        out[--n] = 0;
    return 1;
}

static int reg_str(HKEY root, const wchar_t *key, const wchar_t *name, wchar_t *out)
{
    DWORD n = MAX_PATH * sizeof(wchar_t), type;
    HKEY k;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | KEY_WOW64_64KEY, &k))
        return 0;
    int ok = !RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &n) && type == REG_SZ;
    RegCloseKey(k);
    if (ok)
        out[min(n / sizeof(wchar_t), MAX_PATH - 1)] = 0;
    return ok;
}

int vg_find_game(const wchar_t *hint, wchar_t *out)
{
    wchar_t p[MAX_PATH], me[MAX_PATH];
    if (hint && try_dir(hint, out))
        return 1;
    GetCurrentDirectoryW(MAX_PATH, p);
    if (try_dir(p, out))
        return 1;
    GetModuleFileNameW(NULL, me, MAX_PATH);
    wchar_t *sl = wcsrchr(me, L'\\');
    if (sl) {
        *sl = 0;
        if (try_dir(me, out))
            return 1;
        if ((sl = wcsrchr(me, L'\\'))) {
            *sl = 0;
            if (try_dir(me, out))
                return 1;
        }
    }
    static const wchar_t *const common[] = {L"%lc:\\GOG Games\\Dishonored 2", L"%lc:\\Games\\Dishonored 2",
                                            L"%lc:\\Dishonored 2",
                                            L"%lc:\\Program Files (x86)\\GOG Galaxy\\Games\\Dishonored 2"};
    for (wchar_t drive = L'C'; drive <= L'H'; drive++)
        for (int i = 0; i < 4; i++) {
            _snwprintf(p, MAX_PATH, common[i], drive);
            if (try_dir(p, out))
                return 1;
        }
    // any GOG game folder
    static const wchar_t *const gogs[] = {L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"SOFTWARE\\GOG.com\\Games"};
    for (int r = 0; r < 2; r++) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, gogs[r], 0, KEY_READ | KEY_WOW64_64KEY, &k))
            continue;
        wchar_t sub[256], key[512];
        for (DWORD i = 0;; i++) {
            DWORD n = 256;
            if (RegEnumKeyExW(k, i, sub, &n, NULL, NULL, NULL, NULL))
                break;
            _snwprintf(key, 512, L"%ls\\%ls", gogs[r], sub);
            if (reg_str(HKEY_LOCAL_MACHINE, key, L"path", p) && try_dir(p, out)) {
                RegCloseKey(k);
                return 1;
            }
        }
        RegCloseKey(k);
    }
    // Steam libraries
    wchar_t steam[MAX_PATH];
    if (reg_str(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", steam)) {
        _snwprintf(p, MAX_PATH, L"%ls\\steamapps\\common\\Dishonored2", steam);
        if (try_dir(p, out))
            return 1;
        _snwprintf(p, MAX_PATH, L"%ls\\steamapps\\libraryfolders.vdf", steam);
        size_t n;
        char *vdf = (char *)read_all(p, &n);
        for (char *q = vdf; q && (q = strstr(q, "\"path\""));) {
            q += 6;
            char *a = strchr(q, '"'), *b2 = a ? strchr(a + 1, '"') : NULL;
            if (!b2)
                break;
            char lib[MAX_PATH];
            size_t ln = min((size_t)(b2 - a - 1), sizeof lib - 1), m = 0;
            for (size_t i = 0; i < ln; i++)  // "D:\\SteamLibrary" in the file
                if (!(a[1 + i] == '\\' && i + 1 < ln && a[2 + i] == '\\'))
                    lib[m++] = a[1 + i];
            lib[m] = 0;
            wchar_t wl[MAX_PATH];
            MultiByteToWideChar(CP_UTF8, 0, lib, -1, wl, MAX_PATH);
            _snwprintf(p, MAX_PATH, L"%ls\\steamapps\\common\\Dishonored2", wl);
            if (try_dir(p, out)) {
                free(vdf);
                return 1;
            }
            q = b2 + 1;
        }
        free(vdf);
    }
    return 0;
}

int vg_check_build(const wchar_t *game, int *bad, int *n)
{
    wchar_t p[MAX_PATH];
    jpath(p, game, "Dishonored2.exe");
    uint8_t hdr[4096];
    *bad = 0, *n = (int)(sizeof VG_SIG / sizeof VG_SIG[0]);
    if (!read_at(p, 0, hdr, sizeof hdr)) {
        *bad = *n;
        return 0;
    }
    uint32_t pe = le32(hdr + 0x3C);
    if (pe > sizeof hdr - 0x200) {
        *bad = *n;
        return 0;
    }
    int nsec = hdr[pe + 6] | hdr[pe + 7] << 8, optsz = hdr[pe + 20] | hdr[pe + 21] << 8;
    uint64_t base = (uint64_t)le32(hdr + pe + 24 + 28) << 32 | le32(hdr + pe + 24 + 24);
    for (int k = 0; k < *n; k++) {
        uint64_t va = VG_SIG[k].va;
        int found = 0;
        uint8_t got[16];
        for (int i = 0; i < nsec && pe + 24 + optsz + 40 * (i + 1) <= sizeof hdr; i++) {
            const uint8_t *s = hdr + pe + 24 + optsz + 40 * i;
            uint32_t vsz = le32(s + 8), sva = le32(s + 12), rsz = le32(s + 16), raw = le32(s + 20);
            uint32_t size = vsz < rsz ? vsz : rsz;
            if (base + sva <= va && va + 16 <= base + sva + size)
                found = read_at(p, raw + (va - base - sva), got, 16);
        }
        *bad += !found || memcmp(got, VG_SIG[k].b, 16) != 0;
    }
    return *bad == 0;
}
