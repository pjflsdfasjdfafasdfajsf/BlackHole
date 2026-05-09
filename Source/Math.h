#ifndef MATH_H
#define MATH_H

#include <SDL3/SDL.h>

#define CORNERFLAG_TOPLEFT     (1 << 0)
#define CORNERFLAG_TOPRIGHT    (1 << 1)
#define CORNERFLAG_BOTTOMRIGHT (1 << 2)
#define CORNERFLAG_BOTTOMLEFT  (1 << 3)
#define CORNERFLAG_ALL         (CORNERFLAG_TOPLEFT | CORNERFLAG_TOPRIGHT | CORNERFLAG_BOTTOMRIGHT | CORNERFLAG_BOTTOMLEFT)
#define CORNERFLAG_TOP         (CORNERFLAG_TOPLEFT | CORNERFLAG_TOPRIGHT)
#define CORNERFLAG_BOTTOM      (CORNERFLAG_BOTTOMLEFT | CORNERFLAG_BOTTOMRIGHT)

#define TRANSPARENT (SDL_FColor){ 0, 0, 0, 0 }
#define WHITE       (SDL_FColor){ 0.980f, 0.980f, 0.984f, 1.0f }
#define GRAY        (SDL_FColor){ 0.922f, 0.922f, 0.929f, 1.0f }
#define DARKGRAY    (SDL_FColor){ 0.294f, 0.310f, 0.318f, 1.0f }
#define BLACK       (SDL_FColor){ 0, 0, 0, 1 }
#define RED         (SDL_FColor){ 1, 0, 0, 1 }
#define GREEN       (SDL_FColor){ 0, 1, 0, 1 }
#define BLUE        (SDL_FColor){ 0, 0, 1, 1 }

struct Vector3
{
    float x, y, z;
};

static struct Vector3 Vector3(float x, float y, float z)
{
    return (struct Vector3){ x, y, z };
}

struct Matrix4x4 
{
	float m[4][4];
};

static struct Matrix4x4 Matrix4x4(
    float m00, float m10, float m20, float m30,
    float m01, float m11, float m21, float m31,
    float m02, float m12, float m22, float m32,
    float m03, float m13, float m23, float m33
) {
    return (struct Matrix4x4) {
        .m[0][0] = m00, .m[1][0] = m10, .m[2][0] = m20, .m[3][0] = m30,
        .m[0][1] = m01, .m[1][1] = m11, .m[2][1] = m21, .m[3][1] = m31,
        .m[0][2] = m02, .m[1][2] = m12, .m[2][2] = m22, .m[3][2] = m32,
        .m[0][3] = m03, .m[1][3] = m13, .m[2][3] = m23, .m[3][3] = m33
    };
}

// static struct Matrix4x4 IdentityMatrix(void)
// {
//     return Matrix4x4(
//         1,  0,  0,  0,
//         0,  1,  0,  0,
//         0,  0,  1,  0,
//         0,  0,  0,  1
//     );   
// }

static struct Matrix4x4 OrthographicMatrix(float left, float right, float bottom, float top, float near, float far)
{
    float l = left, r = right, b = bottom, t = top, n = near, f = far;
    float dx = -(r + l) / (r - l);
    float dy = -(t + b) / (t - b);
    float dz = -(f + n) / (f - n);

    return Matrix4x4(
         2 / (r - l),            0,            0,   0,
                   0,  2 / (t - b),            0,   0,
                   0,            0,  2 / (f - n),   0,
                  dx,           dy,           dz,   1
    );
}

static struct Matrix4x4 TranslationMatrix(struct Vector3 offset)
{
    return Matrix4x4(
              1,       0,       0,  0,
              0,       1,       0,  0,
              0,       0,       1,  0,
        offset.x, offset.y, offset.z, 1
    );
}

struct Vertex
{
	float X, Y, Z;
	float R, G, B, A;
	float U, V;
};

#endif