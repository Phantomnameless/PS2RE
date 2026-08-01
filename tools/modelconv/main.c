#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * Model converter: PS2 game model formats → ps2re binary format.
 *
 * PS2 games used a variety of proprietary model formats:
 *   - VIF tag streams (direct VU1 input)
 *   - PMF/OMD/OMO (various game-specific formats)
 *   - General approach: vertex data packed via VIF UNPACK
 *
 * This tool converts raw PS2 model data to a clean binary format:
 *
 *   Header:
 *     magic:      4 bytes ("PS2M")
 *     version:    4 bytes
 *     mesh_count: 4 bytes
 *     bbox_min:   3× float
 *     bbox_max:   3× float
 *
 *   Per mesh:
 *     vertex_count:  4 bytes
 *     index_count:   4 bytes
 *     material_id:   4 bytes
 *     vertex_stride: 4 bytes
 *     vertices:      vertex_count × vertex_stride bytes
 *     indices:       index_count × 2 bytes (u16)
 */

#define MAGIC_PS2M 0x5053324D

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t mesh_count;
    float    bbox_min[3];
    float    bbox_max[3];
} ModelHeader;

typedef struct {
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t material_id;
    uint32_t vertex_stride;
} MeshHeader;

typedef struct {
    float pos[4];      /* x, y, z, w */
    float normal[3];
    float uv[2];
    uint8_t color[4];  /* RGBA8 */
} OutputVertex;  /* 40 bytes — matches VU1Vertex in renderer */

#pragma pack(pop)

/* ── PS2 VIF UNPACK emulation ────────────────────────── */

static void unpack_v4_32_to_vertex(OutputVertex* dst,
                                   const float* src,
                                   int component_mask,
                                   int vertex_index)
{
    /* VIF UNPACK V4-32: 4 floats per vertex */
    if (component_mask & 0x1) dst[vertex_index].pos[0] = src[0];
    if (component_mask & 0x2) dst[vertex_index].pos[1] = src[1];
    if (component_mask & 0x4) dst[vertex_index].pos[2] = src[2];
    if (component_mask & 0x8) dst[vertex_index].pos[3] = src[3];
}

/* ── Compute normals from triangle soup ──────────────── */

static void compute_normals(OutputVertex* verts, int vertex_count,
                            const uint16_t* indices, int index_count)
{
    /* Zero normals */
    for (int i = 0; i < vertex_count; i++) {
        verts[i].normal[0] = 0;
        verts[i].normal[1] = 0;
        verts[i].normal[2] = 0;
    }

    /* Accumulate face normals */
    for (int t = 0; t < index_count; t += 3) {
        uint16_t i0 = indices[t + 0];
        uint16_t i1 = indices[t + 1];
        uint16_t i2 = indices[t + 2];

        float* p0 = verts[i0].pos;
        float* p1 = verts[i1].pos;
        float* p2 = verts[i2].pos;

        /* Edge vectors */
        float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };

        /* Cross product */
        float n[3] = {
                e1[1]*e2[2] - e1[2]*e2[1],
                e1[2]*e2[0] - e1[0]*e2[2],
                e1[0]*e2[1] - e1[1]*e2[0],
        };

        /* Accumulate */
        for (int v = 0; v < 3; v++) {
            int idx = indices[t + v];
            verts[idx].normal[0] += n[0];
            verts[idx].normal[1] += n[1];
            verts[idx].normal[2] += n[2];
        }
    }

    /* Normalize */
    for (int i = 0; i < vertex_count; i++) {
        float* n = verts[i].normal;
        float len = n[0]*n[0] + n[1]*n[1] + n[2]*n[2];
        if (len > 0.0001f) {
            len = 1.0f / __builtin_sqrtf(len);
            n[0] *= len;
            n[1] *= len;
            n[2] *= len;
        }
    }
}

/* ── Compute bounding box ────────────────────────────── */

static void compute_bbox(const OutputVertex* verts, int count,
                         float* out_min, float* out_max)
{
    out_min[0] = out_min[1] = out_min[2] =  1e30f;
    out_max[0] = out_max[1] = out_max[2] = -1e30f;

    for (int i = 0; i < count; i++) {
        for (int c = 0; c < 3; c++) {
            if (verts[i].pos[c] < out_min[c]) out_min[c] = verts[i].pos[c];
            if (verts[i].pos[c] > out_max[c]) out_max[c] = verts[i].pos[c];
        }
    }
}

/* ── Main ────────────────────────────────────────────── */

static void print_usage(const char* prog)
{
    fprintf(stderr,
            "Usage: %s <input.raw> <output.psm> [options]\n"
            "\n"
            "Converts PS2 raw model data to ps2re binary format.\n"
            "\n"
            "Options:\n"
            "  --vertices N     Number of vertices (default: auto-detect)\n"
            "  --indices N      Number of indices (default: auto-detect)\n"
            "  --no-normals     Skip normal computation\n"
            "  --no-indices     Generate sequential indices\n"
            "  --flip-winding   Reverse triangle winding order\n",
            prog);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_path  = argv[1];
    const char* output_path = argv[2];

    int vert_count = 0;
    int idx_count  = 0;
    bool compute_norms = true;
    bool gen_indices   = false;
    bool flip_winding  = false;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--vertices") == 0 && i + 1 < argc) {
            vert_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--indices") == 0 && i + 1 < argc) {
            idx_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-normals") == 0) {
            compute_norms = false;
        } else if (strcmp(argv[i], "--no-indices") == 0) {
            gen_indices = true;
        } else if (strcmp(argv[i], "--flip-winding") == 0) {
            flip_winding = true;
        }
    }

    /* Read input */
    FILE* fin = fopen(input_path, "rb");
    if (!fin) { perror("open input"); return 1; }

    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t* raw_data = malloc((size_t)file_size);
    if (!raw_data) { fclose(fin); return 1; }
    fread(raw_data, 1, (size_t)file_size, fin);
    fclose(fin);

    /* Auto-detect vertex count if not specified */
    /* PS2 models typically: 4 floats pos + 3 floats normal + 2 floats uv + 4 bytes color */
    int input_stride = 16;  /* default: just position (V4-32) */
    if (vert_count == 0) {
        vert_count = (int)(file_size / input_stride);
    }

    if (gen_indices) {
        idx_count = vert_count;
    }

    printf("modelconv: %d vertices, %d indices\n", vert_count, idx_count);

    /* Allocate output vertices */
    OutputVertex* verts = calloc((size_t)vert_count, sizeof(OutputVertex));
    uint16_t* indices = NULL;

    if (idx_count > 0) {
        indices = calloc((size_t)idx_count, sizeof(uint16_t));
    }

    /* Parse input — simplified: assume raw float4 positions */
    const float* src_f = (const float*)raw_data;
    for (int i = 0; i < vert_count; i++) {
        verts[i].pos[0] = src_f[i * 4 + 0];
        verts[i].pos[1] = src_f[i * 4 + 1];
        verts[i].pos[2] = src_f[i * 4 + 2];
        verts[i].pos[3] = 1.0f;

        /* Default UV (can be overridden) */
        verts[i].uv[0] = src_f[i * 4 + 0];  /* planar mapping fallback */
        verts[i].uv[1] = src_f[i * 4 + 1];

        /* Default color: white */
        verts[i].color[0] = 255;
        verts[i].color[1] = 255;
        verts[i].color[2] = 255;
        verts[i].color[3] = 255;
    }

    /* Generate indices */
    if (gen_indices) {
        for (int i = 0; i < vert_count; i++) {
            indices[i] = (uint16_t)i;
        }
    } else if (indices && idx_count > 0) {
        /* Read indices from after vertex data */
        size_t vert_data_size = (size_t)vert_count * input_stride;
        if (vert_data_size + idx_count * 2 <= (size_t)file_size) {
            memcpy(indices, raw_data + vert_data_size,
                   (size_t)idx_count * 2);
        }

        /* Flip winding if requested */
        if (flip_winding) {
            for (int t = 0; t + 2 < idx_count; t += 3) {
                uint16_t tmp = indices[t + 1];
                indices[t + 1] = indices[t + 2];
                indices[t + 2] = tmp;
            }
        }
    }

    /* Compute normals */
    if (compute_norms && indices) {
        compute_normals(verts, vert_count, indices, idx_count);
    }

    /* Compute bounding box */
    float bbox_min[3], bbox_max[3];
    compute_bbox(verts, vert_count, bbox_min, bbox_max);

    /* Write output */
    FILE* fout = fopen(output_path, "wb");
    if (!fout) { perror("open output"); free(raw_data); free(verts); free(indices); return 1; }

    ModelHeader header = {
            .magic      = MAGIC_PS2M,
            .version    = 1,
            .mesh_count = 1,
            .bbox_min   = { bbox_min[0], bbox_min[1], bbox_min[2] },
            .bbox_max   = { bbox_max[0], bbox_max[1], bbox_max[2] },
    };
    fwrite(&header, sizeof(header), 1, fout);

    MeshHeader mesh = {
            .vertex_count  = (uint32_t)vert_count,
            .index_count   = (uint32_t)idx_count,
            .material_id   = 0,
            .vertex_stride = sizeof(OutputVertex),
    };
    fwrite(&mesh, sizeof(mesh), 1, fout);
    fwrite(verts, sizeof(OutputVertex), (size_t)vert_count, fout);

    if (indices && idx_count > 0) {
        fwrite(indices, sizeof(uint16_t), (size_t)idx_count, fout);
    }

    fclose(fout);
    free(raw_data);
    free(verts);
    free(indices);

    printf("modelconv: wrote %s (%.1f KB)\n", output_path,
           (float)(sizeof(header) + sizeof(mesh) +
                   vert_count * sizeof(OutputVertex) +
                   idx_count * 2) / 1024.0f);

    return 0;
}