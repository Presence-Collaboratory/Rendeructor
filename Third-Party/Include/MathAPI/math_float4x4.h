// Description: 4x4 matrix class with comprehensive mathematical operations,
//              SSE optimization, and Row-Major layout for DirectX compatibility.
// Author: NSDeathman, DeepSeek
#pragma once

#include <cmath>
#include <string>
#include <cstdio>
#include <algorithm>
#include <xmmintrin.h>
#include <pmmintrin.h>

#include "math_config.h"
#include "math_constants.h"
#include "math_functions.h"
#include "math_float3.h"
#include "math_float4.h"

namespace Math
{
    class float3x3;
    class quaternion;

    /**
     * @class float4x4
     * @brief 4x4 matrix class stored in Row-Major order.
     *
     * Supports operations in order: MVP = Model * View * Projection.
     * Internally stores 4 rows. optimized for SIMD.
     *
     * @note Layout: Row-Major (DirectX Style).
     * @note Row 3 contains Translation (x, y, z, 1).
     */
    class MATH_API float4x4
    {
    public:
        // Store matrix as four float4 rows
        alignas(16) float4 row0_;
        alignas(16) float4 row1_;
        alignas(16) float4 row2_;
        alignas(16) float4 row3_; // Translation is here

    public:
        // ============================================================================
        // Constructors
        // ============================================================================

        float4x4() noexcept;

        /** @brief Construct from rows */
        float4x4(const float4& r0, const float4& r1, const float4& r2, const float4& r3) noexcept;

        /** @brief Construct from 16 scalars (Row-Major input) */
        float4x4(float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23,
            float m30, float m31, float m32, float m33) noexcept;

        explicit float4x4(const float* data) noexcept;
        explicit float4x4(float scalar) noexcept;
        explicit float4x4(const float4& diagonal) noexcept;
        explicit float4x4(const float3x3& mat3x3) noexcept;
        explicit float4x4(const quaternion& q) noexcept;

#if defined(MATH_SUPPORT_D3DX)
        float4x4(const D3DXMATRIX& mat) noexcept;
        float4x4& operator=(const D3DXMATRIX& mat) noexcept;
#endif

        // ============================================================================
        // Static Constructors
        // ============================================================================

        static float4x4 identity() noexcept;
        static float4x4 zero() noexcept;

        // --- Transformations (Row-Major specific) ---
        static float4x4 translation(const float3& translation) noexcept;
        static float4x4 translation(float x, float y, float z) noexcept;

        static float4x4 scaling(const float3& scale) noexcept;
        static float4x4 scaling(float x, float y, float z) noexcept;
        static float4x4 scaling(float uniformScale) noexcept;

        static float4x4 rotation_x(float angle) noexcept;
        static float4x4 rotation_y(float angle) noexcept;
        static float4x4 rotation_z(float angle) noexcept;
        static float4x4 rotation_axis(const float3& axis, float angle) noexcept;
        static float4x4 rotation_euler(const float3& angles) noexcept; // Z*Y*X order

        static float4x4 TRS(const float3& t, const quaternion& r, const float3& s) noexcept;

        // --- Projections (All Variants) ---

        // 1. Left-Handed, Depth 0..1 (DirectX Standard)
        static float4x4 perspective_lh_zo(float fovY, float aspect, float zNear, float zFar) noexcept;

        // 2. Right-Handed, Depth 0..1 (Vulkan / Modern OpenGL)
        static float4x4 perspective_rh_zo(float fovY, float aspect, float zNear, float zFar) noexcept;

        // 3. Left-Handed, Depth -1..1
        static float4x4 perspective_lh_no(float fovY, float aspect, float zNear, float zFar) noexcept;

        // 4. Right-Handed, Depth -1..1 (Legacy OpenGL)
        static float4x4 perspective_rh_no(float fovY, float aspect, float zNear, float zFar) noexcept;

        inline float4x4 perspective(float fov, float ar, float zn, float zf) noexcept;

        // Generic aliases (Defaults to DX style LH_ZO)
        // 1. Стандартная ортогональная (LH, 0..1)
        static float4x4 orthographic_lh_zo(float width, float height, float zNear, float zFar) noexcept;

        // 2. Off-Center (если нужно, например для UI или теней)
        static float4x4 orthographic_off_center_lh_zo(float left, float right, float bottom, float top, float zNear, float zFar) noexcept;

        // 3. Алиас (Generic) - вызывает orthographic_lh_zo
        static float4x4 orthographic(float width, float height, float zNear, float zFar) noexcept;

        // --- Camera ---
        static float4x4 look_at_lh(const float3& eye, const float3& target, const float3& up) noexcept;
        static float4x4 look_at_rh(const float3& eye, const float3& target, const float3& up) noexcept;

        // Alias (Defaults to LH)
        static float4x4 look_at(const float3& eye, const float3& target, const float3& up) noexcept;

        // ============================================================================
        // Operators
        // ============================================================================

        float4& operator[](int rowIndex) noexcept;
        const float4& operator[](int rowIndex) const noexcept;
        float& operator()(int row, int col) noexcept;
        const float& operator()(int row, int col) const noexcept;

        // Math ops
        float4x4& operator+=(const float4x4& rhs) noexcept;
        float4x4& operator-=(const float4x4& rhs) noexcept;
        float4x4& operator*=(float scalar) noexcept;
        float4x4& operator/=(float scalar) noexcept;

        // Matrix Multiplication (Row-Major: A * B)
        float4x4& operator*=(const float4x4& rhs) noexcept;
        float4x4 operator*(const float4x4& rhs) const noexcept;

        float4x4 operator+(const float4x4& rhs) const noexcept;
        float4x4 operator-(const float4x4& rhs) const noexcept;
        float4x4 operator-() const noexcept;

        // ============================================================================
        // Operations
        // ============================================================================

        float4x4 transposed() const noexcept;
        float determinant() const noexcept;
        float4x4 inverted() const noexcept;
        float4x4 adjugate() const noexcept;
        float3x3 normal_matrix() const noexcept; // For lighting (inverse transpose of rotation part)

        float trace() const noexcept;
        float frobenius_norm() const noexcept;

        // ============================================================================
        // Vector Transforms
        // ============================================================================

        // Transform Vector (v * M) - Natural for Row-Major
        float4 transform_vector(const float4& vec) const noexcept;
        float3 transform_point(const float3& point) const noexcept;
        float3 transform_vector(const float3& vec) const noexcept;
        float3 transform_direction(const float3& dir) const noexcept;

        // ============================================================================
        // Accessors & Decomposition
        // ============================================================================

        float4 row0() const noexcept { return row0_; }
        float4 row1() const noexcept { return row1_; }
        float4 row2() const noexcept { return row2_; }
        float4 row3() const noexcept { return row3_; }

        void set_row0(const float4& r) { row0_ = r; }
        void set_row1(const float4& r) { row1_ = r; }
        void set_row2(const float4& r) { row2_ = r; }
        void set_row3(const float4& r) { row3_ = r; }

        // Columns (Expensive in Row-Major)
        float4 col0() const noexcept;
        float4 col1() const noexcept;
        float4 col2() const noexcept;
        float4 col3() const noexcept;

        float3 get_translation() const noexcept;
        float3 get_scale() const noexcept;
        quaternion get_rotation() const noexcept;

        void set_translation(const float3& t) noexcept;
        void set_scale(const float3& s) noexcept;

        // ============================================================================
        // Utility
        // ============================================================================

        bool is_identity(float epsilon = Constants::Constants<float>::Epsilon) const noexcept;
        bool is_affine(float epsilon = Constants::Constants<float>::Epsilon) const noexcept;
        bool is_orthogonal(float epsilon = Constants::Constants<float>::Epsilon) const noexcept;
        bool approximately(const float4x4& other, float epsilon = Constants::Constants<float>::Epsilon) const noexcept;
        bool approximately_zero(float epsilon = Constants::Constants<float>::Epsilon) noexcept;

        std::string to_string() const;

        void to_column_major_array(float* data) const noexcept;
        void to_row_major_array(float* data) const noexcept;

        bool operator==(const float4x4& rhs) const noexcept;
        bool operator!=(const float4x4& rhs) const noexcept;

#if defined(MATH_SUPPORT_D3DX)
        operator D3DXMATRIX() const noexcept;
#endif
    };

    // Global operators
    inline float4x4 operator*(const float4x4& lhs, float scalar) noexcept { return float4x4(lhs) *= scalar; }
    inline float4x4 operator*(float scalar, const float4x4& rhs) noexcept { return float4x4(rhs) *= scalar; }

    // Vector * Matrix (Row Vector transform)
    inline float4 operator*(const float4& v, const float4x4& m) noexcept { return m.transform_vector(v); }
    inline float3 operator*(const float3& p, const float4x4& m) noexcept { return m.transform_point(p); }

    // Helpers
    inline float4x4 transpose(const float4x4& m) { return m.transposed(); }
    inline float4x4 inverse(const float4x4& m) { return m.inverted(); }
    inline float determinant(const float4x4& m) { return m.determinant(); }

    extern const float4x4 float4x4_Identity;
    extern const float4x4 float4x4_Zero;
}

#include "math_float4x4.inl"
