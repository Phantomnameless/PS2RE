#include "ps2re/math/vec4_neon.h"
#include "ps2re/math/mat4_neon.h"
#include <stdio.h>
#include <math.h>

#define ASSERT_FLOAT(a, b, eps) \
    do { if (fabsf((a)-(b)) > (eps)) { \
        printf("FAIL: %f != %f (eps=%f)\n", (a),(b),(eps)); \
        return 1; } } while(0)

static int test_mat4_identity_mul(void)
{
    printf("test_mat4_identity_mul...");
    mat4 I = mat4_identity();
    vec4 v = v4_set(1.0f, 2.0f, 3.0f, 4.0f);
    vec4 r = mat4_mul_vec4(I, v);

    ASSERT_FLOAT(vgetq_lane_f32(r, 0), 1.0f, 0.001f);
    ASSERT_FLOAT(vgetq_lane_f32(r, 1), 2.0f, 0.001f);
    ASSERT_FLOAT(vgetq_lane_f32(r, 2), 3.0f, 0.001f);
    ASSERT_FLOAT(vgetq_lane_f32(r, 3), 4.0f, 0.001f);

    printf(" PASS\n");
    return 0;
}

static int test_dot_product(void)
{
    printf("test_dot_product...");
    vec4 a = v4_set(1, 0, 0, 0);
    vec4 b = v4_set(0, 1, 0, 0);
    vec4 c = v4_dot4(a, b);
    ASSERT_FLOAT(vgetq_lane_f32(c, 0), 0.0f, 0.001f);

    vec4 d = v4_set(1, 2, 3, 4);
    vec4 e = v4_set(5, 6, 7, 8);
    vec4 f = v4_dot4(d, e);
    /* 1*5 + 2*6 + 3*7 + 4*8 = 5+12+21+32 = 70 */
    ASSERT_FLOAT(vgetq_lane_f32(f, 0), 70.0f, 0.01f);

    printf(" PASS\n");
    return 0;
}

static int test_cross_product(void)
{
    printf("test_cross_product...");
    vec4 x = v4_set(1, 0, 0, 0);
    vec4 y = v4_set(0, 1, 0, 0);
    vec4 z = v4_cross(x, y);

    ASSERT_FLOAT(vgetq_lane_f32(z, 0), 0.0f, 0.001f);
    ASSERT_FLOAT(vgetq_lane_f32(z, 1), 0.0f, 0.001f);
    ASSERT_FLOAT(vgetq_lane_f32(z, 2), 1.0f, 0.001f);

    printf(" PASS\n");
    return 0;
}

static int test_clip_flags(void)
{
    printf("test_clip_flags...");
    /* Point inside frustum */
    vec4 inside = v4_set(0, 0, 0.5f, 1.0f);
    u32 f = v4_clip_flags(inside);
    ASSERT_FLOAT((float)f, 0.0f, 0.001f);

    /* Point behind near plane */
    vec4 behind = v4_set(0, 0, -1.0f, 1.0f);
    f = v4_clip_flags(behind);
    ASSERT_FLOAT((float)(f & CLIP_NEAR), (float)CLIP_NEAR, 0.001f);

    printf(" PASS\n");
    return 0;
}

int main(void)
{
    printf("=== NEON Math Tests ===\n");
    int failures = 0;
    failures += test_mat4_identity_mul();
    failures += test_dot_product();
    failures += test_cross_product();
    failures += test_clip_flags();

    if (failures == 0)
        printf("All tests passed.\n");
    else
        printf("%d test(s) FAILED.\n", failures);
    return failures;
}