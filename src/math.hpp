// ===========================================================================
//  ascent-ios / math.hpp
//  Math + view-projection for Standoff 2 (arm64, Unity IL2CPP).
//
//  Ported 1:1 from wonderful/structs.hpp (same game build):
//    TMatrix  : rotation[0x00], position[0x10], scale[0x20]  (4 floats each)
//    camera   : c_matrix (_11.._44, row-major) -> WorldToScreen
//    marker   : mtx_checker (6 floats)
// ===========================================================================
#pragma once

#include <cmath>
#include <cstdint>

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Vec2 {
    float x = 0.f, y = 0.f;
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

inline float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline bool finite3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Transform block found by the "playerpattern" scan. The pattern matches the
// `scale` field, so the block base is (match_addr - 0x20).
struct TMatrix {
    float rotation[4];   // 0x00
    float position[4];   // 0x10
    float scale[4];      // 0x20
};

// Camera marker ("mtx_checker"). a2 is the FOV; a0/a4 are the near-plane pair.
struct MtxChecker {
    float a0, a1, a2, a3, a4, a5;

    bool valid() const {
        if (a1 != 0.f || a3 != 0.f || a5 != 0.f) return false;          // exactly 0
        if (!std::isfinite(a0) || !std::isfinite(a2) || !std::isfinite(a4)) return false;
        if (a0 > -0.05f || a0 < -1.0f) return false;
        if (a2 < 10.f || a2 > 170.f) return false;                      // FOV
        if (a4 < 0.5f || a4 > 2.0f) return false;
        return true;
    }
};

// View-projection matrix (row-major: m[0]=_11 ... m[15]=_44).
struct Mat4 {
    float m[16];

    bool w2s(const Vec3& p, Vec2& out, float screenW, float screenH) const {
        const Vec3 trans{ m[3], m[7], m[11] };   // _14,_24,_34
        const Vec3 right{ m[0], m[4], m[8] };    // _11,_21,_31
        const Vec3 up   { m[1], m[5], m[9] };    // _12,_22,_32

        const float w = dot3(trans, p) + m[15];  // +_44
        if (w < 0.1f) return false;

        const float y = dot3(up, p) + m[13];     // +_42
        const float x = dot3(right, p) + m[12];  // +_41

        out.x = (screenW * 0.5f) * (1.f + x / w);
        out.y = (screenH * 0.5f) * (1.f - y / w);
        return true;
    }

    // Loose sanity: all finite, no garbage magnitudes (see wonderful isSane()).
    bool sane() const {
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(m[i])) return false;
        for (int i = 0; i < 12; ++i)
            if (std::fabs(m[i]) > 1e4f) return false;
        if (std::fabs(m[15]) > 1e6f) return false;
        if (std::fabs(m[12]) > 1e6f || std::fabs(m[13]) > 1e6f || std::fabs(m[14]) > 1e6f) return false;
        if (std::fabs(m[15]) < 1e-6f) return false;

        const float r0 = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        const float r1 = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
        const float r2 = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
        if (r0 < 1e-3f || r1 < 1e-3f || r2 < 1e-3f) return false;

        const float rightLen = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
        const float upLen    = std::sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
        const float transLen = std::sqrt(m[3] * m[3] + m[7] * m[7] + m[11] * m[11]);
        if (rightLen < 1e-3f || upLen < 1e-3f || transLen < 1e-3f) return false;
        return true;
    }

    // Strong test: looks like a real camera view-projection (column 3 ~ column 4,
    // orthonormal frame, no roll, unit forward). See wonderful looksLikeCamera().
    bool looksLikeCamera() const {
        if (!sane()) return false;

        const float c3[3] = { m[2], m[6], m[10] };   // _13,_23,_33
        const float c4[3] = { m[3], m[7], m[11] };   // _14,_24,_34
        for (int i = 0; i < 3; ++i) {
            float den = std::fabs(c3[i]);
            if (den < 1.0f) den = 1.0f;
            if (std::fabs(c4[i] - c3[i]) / den > 0.02f) return false;
        }

        const float rx = m[0], ry = m[4], rz = m[8];
        const float ux = m[1], uy = m[5], uz = m[9];
        const float tx = m[3], ty = m[7], tz = m[11];
        const float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
        const float ul = std::sqrt(ux * ux + uy * uy + uz * uz);
        const float tl = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tl < 0.9f || tl > 1.1f) return false;
        if (rl < 0.4f || rl > 8.0f) return false;
        if (ul < 0.4f || ul > 8.0f) return false;
        if (std::fabs(ry) > 0.06f) return false;                     // no roll
        if (std::fabs(rl / ul - 1.0f) > 0.20f) return false;
        if (std::fabs(rx * ux + ry * uy + rz * uz) > 0.05f * rl * ul) return false;
        if (std::fabs(rx * tx + ry * ty + rz * tz) > 0.08f * rl * tl) return false;
        if (std::fabs(ux * tx + uy * ty + uz * tz) > 0.08f * ul * tl) return false;
        return true;
    }

    Vec3 eye() const { return { m[12], m[13], m[14] }; }  // _41,_42,_43 (camera world pos)
};
