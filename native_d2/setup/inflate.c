// zlib / deflate decoder (RFC 1950 / 1951) for the setup: the game's resources are zlib streams (the text files the
// setup adds its strings to). Written after the structure of Mark Adler's puff.c: canonical Huffman decoding with
// count / symbol tables, stored, fixed and dynamic blocks. Only what the setup needs: whole-buffer in, known size out.
#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t *in;
    size_t inLen, inPos;
    uint32_t bitBuf;
    int bitCnt;
    uint8_t *out;
    size_t outLen, outPos;
} inf_state;

typedef struct {
    short count[16];   // number of codes of each length
    short symbol[288]; // symbols ordered by code
} huffman;

static int bits(inf_state *s, int need, int *val)
{
    uint32_t v = s->bitBuf;
    while (s->bitCnt < need) {
        if (s->inPos >= s->inLen)
            return -1;
        v |= (uint32_t)s->in[s->inPos++] << s->bitCnt;
        s->bitCnt += 8;
    }
    s->bitBuf = v >> need;
    s->bitCnt -= need;
    *val = (int)(v & ((1u << need) - 1));
    return 0;
}

static int stored(inf_state *s)
{
    s->bitBuf = 0, s->bitCnt = 0;  // to a byte boundary
    if (s->inPos + 4 > s->inLen)
        return -1;
    unsigned len = s->in[s->inPos] | s->in[s->inPos + 1] << 8;
    unsigned nlen = s->in[s->inPos + 2] | s->in[s->inPos + 3] << 8;
    s->inPos += 4;
    if (len != (~nlen & 0xFFFF) || s->inPos + len > s->inLen || s->outPos + len > s->outLen)
        return -1;
    memcpy(s->out + s->outPos, s->in + s->inPos, len);
    s->inPos += len, s->outPos += len;
    return 0;
}

static int decode(inf_state *s, const huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int b;
        if (bits(s, 1, &b))
            return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int construct(huffman *h, const short *length, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (int sym = 0; sym < n; sym++)
        if (length[sym])
            h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int codes(inf_state *s, const huffman *lencode, const huffman *distcode)
{
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99,
                                    115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                   12, 12, 13, 13};
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            if (s->outPos >= s->outLen)
                return -1;
            s->out[s->outPos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29)
                return -1;
            int e, len, dsym, dist;
            if (bits(s, lext[sym], &e))
                return -1;
            len = lbase[sym] + e;
            dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30)
                return -1;
            if (bits(s, dext[dsym], &e))
                return -1;
            dist = dbase[dsym] + e;
            if ((size_t)dist > s->outPos || s->outPos + len > s->outLen)
                return -1;
            for (int k = 0; k < len; k++, s->outPos++)
                s->out[s->outPos] = s->out[s->outPos - dist];
        }
    }
}

static int fixed(inf_state *s)
{
    static int built;
    static huffman lencode, distcode;
    if (!built) {
        short lengths[288];
        int sym = 0;
        for (; sym < 144; sym++)
            lengths[sym] = 8;
        for (; sym < 256; sym++)
            lengths[sym] = 9;
        for (; sym < 280; sym++)
            lengths[sym] = 7;
        for (; sym < 288; sym++)
            lengths[sym] = 8;
        construct(&lencode, lengths, 288);
        for (sym = 0; sym < 30; sym++)
            lengths[sym] = 5;
        construct(&distcode, lengths, 30);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(inf_state *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    huffman lencode, distcode;
    int nlen, ndist, ncode, v;
    if (bits(s, 5, &nlen) || bits(s, 5, &ndist) || bits(s, 4, &ncode))
        return -1;
    nlen += 257, ndist += 1, ncode += 4;
    if (nlen > 286 || ndist > 30)
        return -1;
    int index;
    for (index = 0; index < ncode; index++) {
        if (bits(s, 3, &v))
            return -1;
        lengths[order[index]] = (short)v;
    }
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    if (construct(&lencode, lengths, 19) != 0)
        return -1;
    index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &lencode), len = 0, rep;
        if (sym < 0)
            return -1;
        if (sym < 16) {
            lengths[index++] = (short)sym;
            continue;
        }
        if (sym == 16) {
            if (index == 0)
                return -1;
            len = lengths[index - 1];
            if (bits(s, 2, &rep))
                return -1;
            rep += 3;
        } else if (sym == 17) {
            if (bits(s, 3, &rep))
                return -1;
            rep += 3;
        } else {
            if (bits(s, 7, &rep))
                return -1;
            rep += 11;
        }
        if (index + rep > nlen + ndist)
            return -1;
        while (rep--)
            lengths[index++] = (short)len;
    }
    if (lengths[256] == 0)
        return -1;
    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
        return -1;
    return codes(s, &lencode, &distcode);
}

// zlib stream (2-byte header, deflate data, adler32 not checked) -> exactly outLen bytes. 0 on success.
int vg_inflate(const uint8_t *in, size_t inLen, uint8_t *out, size_t outLen)
{
    if (inLen < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 || (in[1] & 0x20))
        return -1;
    inf_state s = {in, inLen, 2, 0, 0, out, outLen, 0};
    int last, type;
    do {
        if (bits(&s, 1, &last) || bits(&s, 2, &type))
            return -1;
        int err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (err)
            return -1;
    } while (!last);
    return s.outPos == outLen ? 0 : -1;
}
