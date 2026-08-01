#ifndef PS2RE_DMA_ENGINE_H
#define PS2RE_DMA_ENGINE_H

#include "ps2re/types.h"

/*
 * DMA engine replacement.
 *
 * PS2 DMA: 10 channels, manual QWORD transfer, tag-based chaining.
 *   Channel 0: VIF0 (VU0)
 *   Channel 1: VIF1 (VU1)
 *   Channel 2: GIF
 *   Channel 3: IPU_FROM
 *   Channel 4: IPU_TO
 *   Channel 5: SIF0 (IOP→EE)
 *   Channel 6: SIF1 (EE→IOP)
 *   Channel 7: SIF2 (bidirectional)
 *   Channel 8: SPR_FROM (scratchpad)
 *   Channel 9: SPR_TO
 *
 * ARM64 replacement:
 *   - Unified memory: no transfers needed for CPU↔GPU
 *   - Async memcpy for CPU-side bulk operations
 *   - Prefetch-based streaming (PRFM) for large datasets
 */

typedef void (*DMACompleteCallback)(void* user_data);

typedef struct DMAChannel {
    _Atomic(bool)  busy;
    _Atomic(bool)  suspended;
    u32            channel_id;
    DMACompleteCallback on_complete;
    void*          user_data;
} DMAChannel;

typedef struct DMAEngine {
    DMAChannel channels[10];
} DMAEngine;

Result dma_init(DMAEngine* dma);
void   dma_destroy(DMAEngine* dma);

/* Async copy — replaces DMA transfer. Uses memcpy + prefetch. */
Result dma_async_copy(DMAEngine* dma, u32 channel,
                      void* dst, const void* src, size_t size,
                      DMACompleteCallback cb, void* user_data);

/* Poll completion (non-blocking) */
bool dma_poll_complete(DMAEngine* dma, u32 channel);

#endif /* PS2RE_DMA_ENGINE_H */