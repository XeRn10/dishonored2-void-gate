// Void Gate portal math, engine-free: shared by the game module (vg.c) and the offline tests (tests/test_portal.c).
//
// A gate is a frame: origin o (gate center on the surface), z = surface normal (out of the surface, the side bodies
// come from), x = the gate's "up" in its plane (the long axis of the oval), y = z cross x. Walking into gate A comes
// out of gate B: a point p, expressed in A's frame as (lx, ly, lz), lands at B with (lx, -ly, -lz) -- a half turn
// about the up axis, so "into A" (-z) becomes "out of B" (+z), up stays up and left/right swap as when turning round.
// The same map carries velocities, view directions and, for the see-through view, the camera (a viewer in front of
// A sees through A what a camera behind B -- the mapped viewer -- sees through B).
#ifndef VG_PORTAL_H
#define VG_PORTAL_H

#include <math.h>

typedef struct {
    float o[3];
    float x[3], y[3], z[3];  // orthonormal, right-handed: x cross y = z
} vg_frame;

static inline float vp_dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline void vp_cross(const float a[3], const float b[3], float r[3])
{
    float t[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    r[0] = t[0], r[1] = t[1], r[2] = t[2];
}
static inline float vp_len(const float a[3]) { return sqrtf(vp_dot(a, a)); }
static inline int vp_norm(float a[3])
{
    float l = vp_len(a);
    if (l < 1e-6f)
        return 0;
    a[0] /= l, a[1] /= l, a[2] /= l;
    return 1;
}

// Gate frame from its surface normal and an "up" hint (projected onto the surface): for a wall gate world up,
// for a floor or ceiling gate the caster's horizontal facing at the cast (the oval's top points away from them).
static inline void vg_frame_make(vg_frame *f, const float pos[3], const float n[3], const float upHint[3])
{
    float z[3] = {n[0], n[1], n[2]};
    if (!vp_norm(z))
        z[0] = 0, z[1] = 0, z[2] = 1;
    float x[3] = {upHint[0], upHint[1], upHint[2]};
    float d = vp_dot(x, z);
    x[0] -= d * z[0], x[1] -= d * z[1], x[2] -= d * z[2];
    if (!vp_norm(x)) {  // hint along the normal: any direction in the plane
        float a[3] = {fabsf(z[2]) < 0.9f ? 0.0f : 1.0f, 0, fabsf(z[2]) < 0.9f ? 1.0f : 0.0f};
        d = vp_dot(a, z);
        x[0] = a[0] - d * z[0], x[1] = a[1] - d * z[1], x[2] = a[2] - d * z[2];
        vp_norm(x);
    }
    float y[3];
    vp_cross(z, x, y);  // z cross x = y keeps (x, y, z) right-handed
    for (int k = 0; k < 3; k++)
        f->o[k] = pos[k], f->x[k] = x[k], f->y[k] = y[k], f->z[k] = z[k];
}

static inline void vg_to_local(const vg_frame *f, const float p[3], float l[3])
{
    float d[3] = {p[0] - f->o[0], p[1] - f->o[1], p[2] - f->o[2]};
    l[0] = vp_dot(d, f->x), l[1] = vp_dot(d, f->y), l[2] = vp_dot(d, f->z);
}

static inline void vg_dir_to_local(const vg_frame *f, const float v[3], float l[3])
{
    float t[3] = {vp_dot(v, f->x), vp_dot(v, f->y), vp_dot(v, f->z)};
    l[0] = t[0], l[1] = t[1], l[2] = t[2];
}

static inline void vg_from_local_dir(const vg_frame *f, const float l[3], float v[3])
{
    float t[3];
    for (int k = 0; k < 3; k++)
        t[k] = l[0] * f->x[k] + l[1] * f->y[k] + l[2] * f->z[k];
    v[0] = t[0], v[1] = t[1], v[2] = t[2];
}

// point near gate a -> the matching point at gate b
static inline void vg_map_point(const vg_frame *a, const vg_frame *b, const float p[3], float out[3])
{
    float l[3];
    vg_to_local(a, p, l);
    l[1] = -l[1], l[2] = -l[2];
    vg_from_local_dir(b, l, out);
    out[0] += b->o[0], out[1] += b->o[1], out[2] += b->o[2];
}

// direction / velocity through a -> b
static inline void vg_map_dir(const vg_frame *a, const vg_frame *b, const float v[3], float out[3])
{
    float l[3];
    vg_dir_to_local(a, v, l);
    l[1] = -l[1], l[2] = -l[2];
    vg_from_local_dir(b, l, out);
}

// How far an upright capsule (radius r, half height hh >= r, axis = world z) reaches along unit direction u
static inline float vg_capsule_extent(const float u[3], float r, float hh)
{
    return r + (hh - r) * fabsf(u[2]);
}

// Where a body that went into gate a comes out of gate b: its center (upright capsule: radius r, half height hh)
// keeps its place across the gate (Portal: come out where you went in), kept inside the oval (half sizes halfW
// across, halfH along the up axis) and put fully in front of b's surface with `margin` to spare.
static inline void vg_exit_body(const vg_frame *a, const vg_frame *b, const float center[3], float r, float hh,
                                float halfW, float halfH, float margin, float out[3])
{
    float l[3];
    vg_to_local(a, center, l);
    l[1] = -l[1];
    float mx = halfH - vg_capsule_extent(b->x, r, hh), my = halfW - vg_capsule_extent(b->y, r, hh);
    if (mx < 0)
        mx = 0;
    if (my < 0)
        my = 0;
    l[0] = l[0] > mx ? mx : l[0] < -mx ? -mx : l[0];
    l[1] = l[1] > my ? my : l[1] < -my ? -my : l[1];
    l[2] = vg_capsule_extent(b->z, r, hh) + margin;
    vg_from_local_dir(b, l, out);
    out[0] += b->o[0], out[1] += b->o[1], out[2] += b->o[2];
}

// yaw / pitch (degrees, game convention: yaw = atan2(fwd.y, fwd.x), pitch positive = looking down) <-> forward
static inline void vg_angles_to_fwd(float yawDeg, float pitchDeg, float f[3])
{
    float y = yawDeg / 57.2957795f, p = pitchDeg / 57.2957795f;
    f[0] = cosf(p) * cosf(y), f[1] = cosf(p) * sinf(y), f[2] = -sinf(p);
}

static inline void vg_fwd_to_angles(const float f[3], float *yawDeg, float *pitchDeg)
{
    float z = f[2] < -1 ? -1 : f[2] > 1 ? 1 : f[2];
    *yawDeg = atan2f(f[1], f[0]) * 57.2957795f;
    *pitchDeg = -asinf(z) * 57.2957795f;
}

// the up vector of a yaw / pitch camera (no roll): (sin p cos y, sin p sin y, cos p)
static inline void vg_angles_to_up(float yawDeg, float pitchDeg, float u[3])
{
    float y = yawDeg / 57.2957795f, p = pitchDeg / 57.2957795f;
    u[0] = sinf(p) * cosf(y), u[1] = sinf(p) * sinf(y), u[2] = cosf(p);
}

// Yaw / pitch for a camera frame turned through the gates (forward f, up u). The yaw from the forward alone is
// undefined looking straight up or down (an exit out of a floor / ceiling gate): a yaw 180 degrees off there turns
// the picture upside down. For a roll-free camera f_h cos p + u_h sin p = (cos y, sin y) at any pitch, so the yaw
// comes from both: from the forward when level, from the up when vertical, whatever the exit.
static inline void vg_view_to_angles(const float f[3], const float u[3], float *yawDeg, float *pitchDeg)
{
    float z = f[2] < -1 ? -1 : f[2] > 1 ? 1 : f[2];
    float p = -asinf(z), cp = cosf(p), sp = sinf(p);
    float hx = f[0] * cp + u[0] * sp, hy = f[1] * cp + u[1] * sp;
    *pitchDeg = p * 57.2957795f;
    *yawDeg = hx * hx + hy * hy > 1e-10f ? atan2f(hy, hx) * 57.2957795f : atan2f(f[1], f[0]) * 57.2957795f;
}

#endif
