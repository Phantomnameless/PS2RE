#include "ps2re/ps2/dma_engine.h"
#include <string.h>

Result dma_init(DMAEngine* dma)
{
    for (int i = 0; i < 10; i++) {
        dma->channels[i].channel_id = (u32)i;
        dma->channels[i].busy = false;
        dma->channels[i].suspended = false;
        dma->channels[i].on_complete = NULL;
        dma->channels[i].user_data = NULL;
    }
    return OK;
}

void dma_destroy(DMAEngine* dma)
{
    (void)dma;
}

/* Streaming copy with prefetch — replaces DMA hardware */
static void streaming_memcpy(void* RESTRICT dst, const void* RESTRICT src, size_t size)
{
    const u8* s = (const u8*)src;
    u8* d = (u8*)dst;

    /* Prefetch distance: 512 bytes ahead */
#define PREFETCH_DIST 512

    size_t i = 0;

    /* 64-byte (cache-line) aligned copies with prefetch */
    for (; i + 64 <= size; i += 64) {
        if (i + PREFETCH_DIST < size) {
            PREFETCH_R(s + i + PREFETCH_DIST);
        }

        /* Load 64 bytes = 4 × 16-byte NEON loads */
        float32x4_t v0 = vld1q_f32((const float*)(s + i));
        float32x4_t v1 = vld1q_f32((const float*)(s + i + 16));
        float32x4_t v2 = vld1q_f32((const float*)(s + i + 32));
        float32x4_t v3 = vld1q_f32((const float*)(s + i + 48));

        vst1q_f32((float*)(d + i),      v0);
        vst1q_f32((float*)(d + i + 16), v1);
        vst1q_f32((float*)(d + i + 32), v2);
        vst1q_f32((float*)(d + i + 48), v3);
    }

    /* Remainder */
    for (; i < size; i++) {
        d[i] = s[i];
    }
}

Result dma_async_copy(DMAEngine* dma, u32 channel,
                      void* dst, const void* src, size_t size,
                      DMACompleteCallback cb, void* user_data)
{
    if (channel >= 10) return ERR_INVALID;
    DMAChannel* ch = &dma->channels[channel];

    if (atomic_load_acq(&ch->busy)) return ERR_BUSY;

    atomic_store_rel(&ch->busy, true);
    ch->on_complete = cb;
    ch->user_data = user_data;

    /* In a real system, this would be dispatched to a DMA thread.
     * For ARM64 unified memory, we do streaming memcpy with prefetch.
     * This is ASYNC from the caller's perspective — they poll completion. */
    streaming_memcpy(dst, src, size);

    atomic_store_rel(&ch->busy, false);
    if (ch->on_complete) ch->on_complete(ch->user_data);
    return OK;
}

bool dma_poll_complete(DMAEngine* dma, u32 channel)
{
    if (channel >= 10) return true;
    return !atomic_load_acq(&dma->channels[channel].busy);
}