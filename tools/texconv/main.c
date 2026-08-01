#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * Texture converter: PS2 formats → ASTC/ETC2/BC7
 *
 * PS2 texture formats:
 *   PSMCT32  — 32-bit RGBA
 *   PSMCT24  — 24-bit RGB
 *   PSMCT16  — 16-bit RGBA (1555, 5551, 4444)
 *   PSMT8    — 8-bit indexed (CLUT)
 *   PSMT4    — 4-bit indexed (CLUT)
 *
 * All PS2 textures are swizzled (tiled) for GS eDRAM access.
 * Step 1: De-swizzle
 * Step 2: Decode to RGBA8
 * Step 3: Encode to ASTC 4×4 (best quality/size for ARM64)
 */

static void deswizzle_ps2(uint8_t* out, const uint8_t* in,
                          int width, int height, int bpp)
{
    int block_width  = (bpp == 32) ? 8 : (bpp == 16) ? 16 : 32;
    int block_height = 8;
    int bytes_per_pixel = bpp / 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int block_x = x / block_width;
            int block_y = y / block_height;
            int in_block_x = x % block_width;
            int in_block_y = y % block_height;

            /* PS2 swizzle pattern */
            int src_offset = ((block_y * (width / block_width) + block_x)
                              * block_width * block_height
                              + in_block_y * block_width
                              + in_block_x) * bytes_per_pixel;

            int dst_offset = (y * width + x) * bytes_per_pixel;

            if (src_offset >= 0 && dst_offset >= 0) {
                memcpy(out + dst_offset, in + src_offset, (size_t)bytes_per_pixel);
            }
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: texconv <input.ps2tex> <output.astc>\n");
        return 1;
    }

    FILE* fin = fopen(argv[1], "rb");
    if (!fin) { perror("open input"); return 1; }

    /* Read header (simplified) */
    uint32_t width, height, format;
    fread(&width, 4, 1, fin);
    fread(&height, 4, 1, fin);
    fread(&format, 4, 1, fin);

    size_t data_size = width * height * 4; /* worst case */
    uint8_t* raw = malloc(data_size);
    uint8_t* rgba = malloc(width * height * 4);
    fread(raw, data_size, 1, fin);
    fclose(fin);

    /* De-swizzle */
    int bpp = (format == 0) ? 32 : (format == 1) ? 24 : 16;
    deswizzle_ps2(rgba, raw, (int)width, (int)height, bpp);

    /* Output as raw RGBA for now — ASTC encoding requires astcenc library */
    FILE* fout = fopen(argv[2], "wb");
    fwrite(&width, 4, 1, fout);
    fwrite(&height, 4, 1, fout);
    fwrite(rgba, width * height * 4, 1, fout);
    fclose(fout);

    free(raw);
    free(rgba);
    printf("Converted: %ux%u\n", width, height);
    return 0;
}