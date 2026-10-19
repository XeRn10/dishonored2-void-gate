// Offline Monte Carlo tests of the Void Gate portal math (vg_portal.h) and of the floor-gate trigger -- no game needed.
//   native_d2\build.cmd test        builds and runs it; exit code = number of failed checks (0 = all good)
//   test_portal.exe [cases] [seed]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vg_portal.h"

static unsigned long long rng = 0x9E3779B97F4A7C15ull;
static float rnd(void)  // [0, 1)
{
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    return (float)((rng >> 11) * (1.0 / 9007199254740992.0));
}
static float rr(float a, float b) { return a + (b - a) * rnd(); }
static void rdir(float v[3])
{
    do
        v[0] = rr(-1, 1), v[1] = rr(-1, 1), v[2] = rr(-1, 1);
    while (vp_dot(v, v) > 1 || !vp_norm(v));
}

// a random gate: wall (horizontal normal, world-up hint), floor, ceiling or slanted surface
static void rgate(vg_frame *f)
{
    float pos[3] = {rr(-50, 50), rr(-50, 50), rr(-10, 30)}, n[3], up[3] = {0, 0, 1};
    int kind = (int)(rnd() * 4);
    if (kind == 0) {
        float a = rr(-3.14159f, 3.14159f);
        n[0] = cosf(a), n[1] = sinf(a), n[2] = 0;
    } else if (kind == 1 || kind == 2) {
        n[0] = rr(-0.05f, 0.05f), n[1] = rr(-0.05f, 0.05f), n[2] = kind == 1 ? 1 : -1;
        vp_norm(n);
        float a = rr(-3.14159f, 3.14159f);  // caster's facing
        up[0] = cosf(a), up[1] = sinf(a), up[2] = 0;
    } else
        rdir(n);
    vg_frame_make(f, pos, n, up);
}

typedef struct {
    const char *name;
    long cases, fails;
    double maxErr;
} result_t;
static result_t R[16];
static int nR;
static result_t *test(const char *name)
{
    R[nR].name = name;
    return &R[nR++];
}
static void check(result_t *t, int ok, double err)
{
    t->cases++;
    if (!ok) {
        if (t->fails < 3)
            fprintf(stderr, "  FAIL %s (case %ld, err %g)\n", t->name, t->cases, err);
        t->fails++;
    }
    if (err > t->maxErr)
        t->maxErr = err;
}
static double dist3(const float a[3], const float b[3])
{
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return vp_len(d);
}

// ---- the floor gate trigger as vg.c has it (player_entered, floor branch) and its vertical speed estimate
static int floor_trigger(float dxy2, float dz, float vz)
{
    if (dxy2 < 0.45f * 0.45f && fabsf(dz) < 0.25f)
        return 1;
    return vz < -3.0f && dxy2 < 0.75f * 0.75f && dz > -0.2f && dz < 0.3f - vz * 0.08f;
}

// the worse of the two angles (degrees) between a yaw / pitch camera's forward and up and a wanted frame
static double frame_err(const float f2[3], const float u2[3], float yaw, float pitch)
{
    float f3[3], u3[3];
    vg_angles_to_fwd(yaw, pitch, f3);
    vg_angles_to_up(yaw, pitch, u3);
    float c1 = vp_dot(f2, f3), c2 = vp_dot(u2, u3);
    double e1 = acos(c1 > 1 ? 1 : c1 < -1 ? -1 : c1), e2 = acos(c2 > 1 ? 1 : c2 < -1 ? -1 : c2);
    return (e1 > e2 ? e1 : e2) * 57.2957795;
}

int main(int argc, char **argv)
{
    long N = argc > 1 ? atol(argv[1]) : 200000;
    if (argc > 2)
        rng = strtoull(argv[2], NULL, 10) | 1;
    const float r = 0.35f, hh = 0.9f, halfW = 0.85f, halfH = 1.4f, eps = 2e-3f;

    result_t *tFrame = test("frame orthonormal, right-handed"), *tRound = test("map a->b->a = identity"),
             *tLen = test("velocity speed kept"), *tInOut = test("into A comes out of B"),
             *tUp = test("wall->wall keeps world up"), *tYaw = test("wall->wall yaw = old rule"),
             *tExit = test("exit body in front of B, inside oval"), *tCam = test("camera: behind B, sees through B"),
             *tSurf = test("gate surface maps onto gate surface"), *tAng = test("yaw/pitch <-> forward"),
             *tView = test("exit view never upside down");
    long nFlipOld = 0, nFlipNew = 0, nVert = 0;
    for (long i = 0; i < N; i++) {
        vg_frame a, b;
        rgate(&a), rgate(&b);
        // frames
        for (int k = 0; k < 2; k++) {
            vg_frame *f = k ? &b : &a;
            float c[3];
            vp_cross(f->x, f->y, c);
            double e = fabs(vp_len(f->x) - 1) + fabs(vp_len(f->y) - 1) + fabs(vp_dot(f->x, f->y)) +
                       fabs(vp_dot(f->x, f->z)) + fabs(vp_dot(f->y, f->z)) + dist3(c, f->z);
            check(tFrame, e < 1e-4, e);
        }
        // round trip of a point near A
        float p[3] = {a.o[0] + rr(-2, 2), a.o[1] + rr(-2, 2), a.o[2] + rr(-2, 2)}, q[3], p2[3];
        vg_map_point(&a, &b, p, q);
        vg_map_point(&b, &a, q, p2);
        check(tRound, dist3(p, p2) < 1e-3, dist3(p, p2));
        // velocity: speed kept; into A (v.zA < 0) -> out of B (v'.zB > 0)
        float v[3], v2[3];
        rdir(v);
        float s = rr(0.1f, 80);
        v[0] *= s, v[1] *= s, v[2] *= s;
        vg_map_dir(&a, &b, v, v2);
        check(tLen, fabsf(vp_len(v) - vp_len(v2)) < 1e-3f * s, fabsf(vp_len(v) - vp_len(v2)));
        if (vp_dot(v, a.z) < 0)
            check(tInOut, vp_dot(v2, b.z) > 0, -vp_dot(v2, b.z));
        // wall -> wall
        if (fabsf(a.z[2]) < 1e-6f && fabsf(b.z[2]) < 1e-6f) {
            float up[3] = {0, 0, 1}, up2[3];
            vg_map_dir(&a, &b, up, up2);
            check(tUp, dist3(up, up2) < 1e-4, dist3(up, up2));
            // old rule: yaw' = yaw + yaw(nB) - (yaw(nA) + 180), for a horizontal view direction
            float yaw = rr(-180, 180), f[3], f2[3], y2, p2d;
            vg_angles_to_fwd(yaw, 0, f);
            vg_map_dir(&a, &b, f, f2);
            vg_fwd_to_angles(f2, &y2, &p2d);
            float old = yaw + atan2f(b.z[1], b.z[0]) * 57.2957795f - (atan2f(a.z[1], a.z[0]) * 57.2957795f + 180);
            float d = fmodf(fabsf(y2 - old), 360);
            d = d > 180 ? 360 - d : d;
            check(tYaw, d < 0.05f, d);
        }
        // exit body: in front of B with the margin, inside the oval
        {
            float c[3] = {a.o[0] + rr(-1, 1), a.o[1] + rr(-1, 1), a.o[2] + rr(-1, 1)}, out[3], l[3];
            vg_exit_body(&a, &b, c, r, hh, halfW, halfH, 0.05f, out);
            vg_to_local(&b, out, l);
            double front = l[2] - vg_capsule_extent(b.z, r, hh);
            double side = fmax(fabs(l[0]) - fmax(0, halfH - vg_capsule_extent(b.x, r, hh)),
                               fabs(l[1]) - fmax(0, halfW - vg_capsule_extent(b.y, r, hh)));
            check(tExit, front > 0.05f - eps && side < eps, fmax(0.05f - front, side));
        }
        // see-through camera: a viewer in front of A maps behind B, and its view ray through a point of A's
        // surface maps to the ray from the virtual camera through the matching point of B's surface
        {
            float e[3], le[3] = {rr(-3, 3), rr(-3, 3), rr(0.2f, 10)}, e2[3], l2[3];
            vg_from_local_dir(&a, le, e);
            e[0] += a.o[0], e[1] += a.o[1], e[2] += a.o[2];
            vg_map_point(&a, &b, e, e2);
            vg_to_local(&b, e2, l2);
            check(tCam, l2[2] < -0.19f, l2[2]);
            float ls[3] = {rr(-halfH, halfH), rr(-halfW, halfW), 0}, sA[3], sB[3], dirA[3], dirB[3], m[3];
            vg_from_local_dir(&a, ls, sA);
            sA[0] += a.o[0], sA[1] += a.o[1], sA[2] += a.o[2];
            vg_map_point(&a, &b, sA, sB);
            vg_to_local(&b, sB, l2);
            check(tSurf, fabsf(l2[2]) < 1e-3f, fabsf(l2[2]));
            for (int k = 0; k < 3; k++)
                dirA[k] = sA[k] - e[k], dirB[k] = sB[k] - e2[k];
            vg_map_dir(&a, &b, dirA, m);
            check(tCam, dist3(m, dirB) < 1e-3 * (1 + vp_len(dirA)), dist3(m, dirB));
        }
        // exit view: the camera after the teleport (yaw / pitch, pitch clamped to 85 as in vg.c) vs the frame turned
        // through the gates (what the player saw through the window). Looking (nearly) straight up or down out of a
        // gate, any up can be matched by the yaw: the screen's up must come out right there (the forward-only yaw was
        // arbitrary there: the "upside down" exits). Elsewhere a roll between the gates can't be matched by a
        // roll-free camera: the frame rule must just never be worse than the forward-only yaw (error = the worse of
        // view direction and up).
        {
            float yaw = rr(-180, 180), pitch = rr(-85, 85), f[3], u[3], f2[3], u2[3], y, pt;
            vg_angles_to_fwd(yaw, pitch, f);
            vg_angles_to_up(yaw, pitch, u);
            vg_map_dir(&a, &b, f, f2);
            vg_map_dir(&a, &b, u, u2);
            double comb[2], up[2];
            for (int m = 0; m < 2; m++) {
                if (m)
                    vg_view_to_angles(f2, u2, &y, &pt);
                else
                    vg_fwd_to_angles(f2, &y, &pt);
                pt = pt > 85 ? 85 : pt < -85 ? -85 : pt;
                comb[m] = frame_err(f2, u2, y, pt);
                float u3[3];
                vg_angles_to_up(y, pt, u3);
                float c = vp_dot(u2, u3);
                up[m] = acos(c > 1 ? 1 : c < -1 ? -1 : c) * 57.2957795;
            }
            int vertical = fabsf(f2[2]) > 0.97f;  // out of a gate looking within ~14 degrees of straight up / down
            if (vertical)
                nVert++, nFlipOld += up[0] > 90, nFlipNew += up[1] > 90;
            check(tView, (!vertical || up[1] < 30) && comb[1] <= comb[0] + 1.0,
                  vertical ? up[1] : comb[1] - comb[0]);
        }
        // angles round trip (away from straight up/down)
        {
            float yaw = rr(-179, 179), pitch = rr(-85, 85), f[3], y2, p2;
            vg_angles_to_fwd(yaw, pitch, f);
            vg_fwd_to_angles(f, &y2, &p2);
            double e = fabs(y2 - yaw) + fabs(p2 - pitch);
            check(tAng, e < 0.01, e);
        }
    }

    // ---- floor gate trigger: drops from up to 100 m at frame rates 30..144 fps, the speed estimate of vg.c
    // (half old, half new), the body stopped by the floor. Must fire before the feet pass the gate (dz < -0.2:
    // the floor catches the player there anyway, then only the "stand in the middle" rule can fire).
    result_t *tFall = test("fast fall into a floor gate fires");
    result_t *tRim = test("standing on the rim does not fire");
    long fallCases = N / 20;
    for (long i = 0; i < fallCases; i++) {
        float h = rr(1.5f, 100), off = rr(0, 0.7f), dt = 1.0f / rr(30, 144);
        float z = h, vz = 0, vzEst = 0, lastZ = z;
        int fired = 0;
        for (int step = 0; step < 100000 && !fired; step++) {
            vz -= 9.81f * 1.6f * dt;  // (the game's gravity is stronger than 9.81)
            z += vz * dt;
            if (z <= 0)
                z = 0, vz = 0;  // the floor
            vzEst = 0.5f * vzEst + 0.5f * (z - lastZ) / dt;
            lastZ = z;
            fired = floor_trigger(off * off, z, vzEst);
            if (z == 0 && vz == 0 && !fired)
                break;
        }
        // from 1.5 m up, at least 4.4 m/s at the floor: a drop should be caught wherever it lands on the gate
        check(tFall, fired || off >= 0.75f, fired || off >= 0.75f ? 0 : off);
    }
    for (long i = 0; i < N / 20; i++) {
        float off = rr(0.46f, 1.5f), dz = rr(-0.25f, 0.25f);
        check(tRim, !floor_trigger(off * off, dz, 0), floor_trigger(off * off, dz, 0) ? off : 0);
    }

    int failed = 0;
    printf("%-40s %10s %8s %12s\n", "check", "cases", "fails", "max err");
    for (int k = 0; k < nR; k++) {
        printf("%-40s %10ld %8ld %12.3g\n", R[k].name, R[k].cases, R[k].fails, R[k].maxErr);
        failed += R[k].fails > 0;
    }
    printf("exits looking up / down: %ld, upside down (screen up > 90 degrees off): forward-only yaw %ld, frame rule %ld\n",
           nVert, nFlipOld, nFlipNew);
    printf(failed ? "%d check(s) FAILED\n" : "all checks passed\n", failed);
    return failed;
}
