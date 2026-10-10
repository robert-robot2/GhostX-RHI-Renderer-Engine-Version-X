// GhostMath.h
#pragma once
#include <math.h>

struct GhostVec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    GhostVec3() = default;
    GhostVec3(float x, float y, float z) : x(x), y(y), z(z) {}
    GhostVec3 operator+(const GhostVec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    GhostVec3 operator-(const GhostVec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    GhostVec3 operator*(float s)            const { return { x * s,   y * s,   z * s }; }
    float Dot(const GhostVec3& o) const { return x * o.x + y * o.y + z * o.z; }
    GhostVec3 Cross(const GhostVec3& o) const
    {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }
    float Length() const { return sqrtf(x * x + y * y + z * z); }
    GhostVec3 Normalize() const
    {
        float len = Length();
        if (len < 1e-6f) return {};
        return { x / len, y / len, z / len };
    }
};

struct GhostMat4
{
    float m[4][4] = {};

    static GhostMat4 Identity()
    {
        GhostMat4 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    GhostMat4 operator*(const GhostMat4& o) const
    {
        GhostMat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                for (int k = 0; k < 4; k++)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }

    static GhostMat4 RotationY(float rad)
    {
        GhostMat4 r = Identity();
        r.m[0][0] = cosf(rad); r.m[0][2] = sinf(rad);
        r.m[2][0] = -sinf(rad); r.m[2][2] = cosf(rad);
        return r;
    }

    static GhostMat4 RotationX(float rad)
    {
        GhostMat4 r = Identity();
        r.m[1][1] = cosf(rad); r.m[1][2] = -sinf(rad);
        r.m[2][1] = sinf(rad); r.m[2][2] = cosf(rad);
        return r;
    }
    static GhostMat4 RotationZ(float rad)
    {
        GhostMat4 r = Identity();
        r.m[0][0] = cosf(rad); r.m[0][1] = -sinf(rad);
        r.m[1][0] = sinf(rad); r.m[1][1] = cosf(rad);
        return r;
    }

    static GhostMat4 Scale(float x, float y, float z)
    {
        GhostMat4 r = Identity();
        r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
        return r;
    }
    static GhostMat4 Translation(float x, float y, float z)
    {
        GhostMat4 r = Identity();
        r.m[0][3] = x; r.m[1][3] = y; r.m[2][3] = z;
        return r;
    }

    static GhostMat4 PerspectiveFovLH(float fovY, float aspect, float zNear, float zFar)
    {
        float yScale = 1.0f / tanf(fovY * 0.5f);
        float xScale = yScale / aspect;
        float zRange = zFar / (zFar - zNear);

        GhostMat4 r;                       // already zero-initialized
        r.m[0][0] = xScale;
        r.m[1][1] = yScale;
        r.m[2][2] = zRange;
        r.m[2][3] = -zNear * zRange;       // ← was 1.0f
        r.m[3][2] = 1.0f;                  // ← was -zNear * zRange
        // m[3][3] stays 0
        return r;
    }

    static GhostMat4 LookAtLH(GhostVec3 eye, GhostVec3 target, GhostVec3 up)
    {
        GhostVec3 f = (target - eye).Normalize();
        GhostVec3 r = up.Cross(f).Normalize();   // was f.Cross(up)
        GhostVec3 u = f.Cross(r);                // was r.Cross(f)
        GhostMat4 mat = Identity();
        mat.m[0][0] = r.x; mat.m[0][1] = r.y; mat.m[0][2] = r.z; mat.m[0][3] = -r.Dot(eye);
        mat.m[1][0] = u.x; mat.m[1][1] = u.y; mat.m[1][2] = u.z; mat.m[1][3] = -u.Dot(eye);
        mat.m[2][0] = f.x; mat.m[2][1] = f.y; mat.m[2][2] = f.z; mat.m[2][3] = -f.Dot(eye);
        return mat;
    }

    GhostMat4 Transpose() const
    {
        GhostMat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                r.m[i][j] = m[j][i];
        return r;
    }
};

struct GhostMVP { GhostMat4 model; GhostMat4 view; GhostMat4 proj; };

#ifndef GHOST_PI
constexpr float GHOST_PI = 3.1415926535f;
#endif