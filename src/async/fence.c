#include "ps2re/async/fence.h"

Result cpu_fence_init(CPUFence* fence)
{
    pthread_mutex_init(&fence->mu, NULL);
    pthread_cond_init(&fence->cv, NULL);
    atomic_init(&fence->signaled, 0);
    atomic_init(&fence->value, (u64)0);
    return OK;
}

void cpu_fence_destroy(CPUFence* fence)
{
    pthread_cond_destroy(&fence->cv);
    pthread_mutex_destroy(&fence->mu);
}

void cpu_fence_signal(CPUFence* fence, u64 value)
{
    pthread_mutex_lock(&fence->mu);
    atomic_store_explicit(&fence->value, value, memory_order_relaxed);
    atomic_store_explicit(&fence->signaled, 1, memory_order_release);
    pthread_cond_broadcast(&fence->cv);
    pthread_mutex_unlock(&fence->mu);
}

void cpu_fence_wait(CPUFence* fence, u64 value)
{
    pthread_mutex_lock(&fence->mu);
    while (atomic_load_explicit(&fence->value, memory_order_acquire) < value) {
        atomic_store_explicit(&fence->signaled, 0, memory_order_relaxed);
        pthread_cond_wait(&fence->cv, &fence->mu);
    }
    pthread_mutex_unlock(&fence->mu);
}

bool cpu_fence_is_ready(const CPUFence* fence, u64 value)
{
    return atomic_load_explicit(&fence->signaled, memory_order_acquire) &&
           atomic_load_explicit(&fence->value, memory_order_acquire) >= value;
}