#include "ps2re/math/vec4_neon.h"

/* vec4_neon.h is all FORCE_INLINE — no .c needed.
 * This file exists for: future non-inline functions, benchmarks, tests.
 * Keeping it ensures the build system has an object to link. */

/* Example: batch normalize (for skinning, particle normals) */
void vec4_normalize_batch(float* RESTRICT xyz_out,
                          const float* RESTRICT xyz_in,
                          int count)
{
    /* SoA: x[], y[], z[] interleaved as stride-3 or separate arrays */
    /* Processing 4 at a time */
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        vec4 x = vld1q_f32(xyz_in + (i * 3) + 0);  /* stride 3 */
        /* ... load y, z similarly ... */
        /* For simplicity, assume AoS with 16-byte aligned vec4 */
        vec4 v = vld1q_f32(xyz_in + i * 4);
        vec4 n = v4_normalize3(v);
        vst1q_f32(xyz_out + i * 4, n);
    }
}