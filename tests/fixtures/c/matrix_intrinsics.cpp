// The kr32::matrix surface lowers to tile instructions instead of calls:
// MLOAD, MMUL, MMAC, MTRANS, MVEC, VDOT, VSTORE, and the transform stack.
// The declarations are the ones the linker would resolve, so a call that was
// not recognised as an intrinsic fails to link. A mat4 returned by value is
// memory class and travels through the hidden destination pointer in R4.
// Returns 42 when every result is exact.

struct vec4 {
    float x;
    float y;
    float z;
    float w;
};

struct mat4 {
    float m[16];
};

namespace kr32 {
mat4 matrix_mul(const mat4& a, const mat4& b);
mat4 matrix_mul_add(const mat4& a, const mat4& b, const mat4& c);
mat4 transpose(const mat4& a);
vec4 transform(const mat4& matrix, const vec4& vector);
float dot(const vec4& a, const vec4& b);
void push(const mat4& matrix);
void pop(mat4& matrix);
void drop();
}

static mat4 translation_matrix(float tx, float ty, float tz) {
    mat4 matrix;
    matrix.m[0] = 1.0;   matrix.m[1] = 0.0;   matrix.m[2] = 0.0;   matrix.m[3] = tx;
    matrix.m[4] = 0.0;   matrix.m[5] = 1.0;   matrix.m[6] = 0.0;   matrix.m[7] = ty;
    matrix.m[8] = 0.0;   matrix.m[9] = 0.0;   matrix.m[10] = 1.0;  matrix.m[11] = tz;
    matrix.m[12] = 0.0;  matrix.m[13] = 0.0;  matrix.m[14] = 0.0;  matrix.m[15] = 1.0;
    return matrix;
}

static mat4 scale_matrix(float factor) {
    mat4 matrix;
    matrix.m[0] = factor; matrix.m[1] = 0.0;    matrix.m[2] = 0.0;    matrix.m[3] = 0.0;
    matrix.m[4] = 0.0;    matrix.m[5] = factor; matrix.m[6] = 0.0;    matrix.m[7] = 0.0;
    matrix.m[8] = 0.0;    matrix.m[9] = 0.0;    matrix.m[10] = factor; matrix.m[11] = 0.0;
    matrix.m[12] = 0.0;   matrix.m[13] = 0.0;   matrix.m[14] = 0.0;   matrix.m[15] = 1.0;
    return matrix;
}

int main() {
    vec4 point;
    point.x = 2.0;
    point.y = 4.0;
    point.z = 6.0;
    point.w = 1.0;

    // MVEC multiplies in column 0; the lowering transposes both ways.
    mat4 translation = translation_matrix(10.0, 20.0, 30.0);
    vec4 moved = kr32::transform(translation, point);
    if (moved.x != 12.0 || moved.y != 24.0 || moved.z != 36.0 || moved.w != 1.0) {
        return 1;
    }

    // MMUL, then the product is used by a second MVEC.
    mat4 scale = scale_matrix(2.0);
    mat4 model = kr32::matrix_mul(translation, scale);
    vec4 placed = kr32::transform(model, point);
    if (placed.x != 14.0 || placed.y != 28.0 || placed.z != 42.0 || placed.w != 1.0) {
        return 2;
    }

    // The product is computed before the assignment overwrites an operand.
    mat4 twice = kr32::matrix_mul(scale, scale);
    scale = kr32::matrix_mul(scale, twice);
    if (scale.m[0] != 8.0 || scale.m[5] != 8.0 || scale.m[3] != 0.0) {
        return 3;
    }

    // MMAC accumulates into its third operand: A * B + C. These matrices are
    // affine, so the translation column survives and the corner stays 2.
    mat4 sum = kr32::matrix_mul_add(translation, scale, scale);
    if (sum.m[0] != 16.0 || sum.m[3] != 10.0 || sum.m[15] != 2.0) {
        return 4;
    }

    // MTRANS moves the translation column into the fourth row.
    mat4 flipped = kr32::transpose(translation);
    if (flipped.m[12] != 10.0 || flipped.m[13] != 20.0 || flipped.m[14] != 30.0 ||
        flipped.m[3] != 0.0) {
        return 5;
    }

    // VDOT reduces the first row of each vector.
    vec4 ones;
    ones.x = 1.0;
    ones.y = 1.0;
    ones.z = 1.0;
    ones.w = 1.0;
    if (kr32::dot(point, ones) != 13.0) {
        return 6;
    }

    // The transform stack is last-in, first-out, and MDROP discards a save
    // without writing it anywhere.
    kr32::push(translation);
    kr32::push(scale);
    mat4 restored;
    kr32::pop(restored);
    if (restored.m[0] != 8.0 || restored.m[5] != 8.0) {
        return 7;
    }
    kr32::drop();

    return 42;
}
