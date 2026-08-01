#ifndef PS2RE_PLATFORM_H
#define PS2RE_PLATFORM_H

/*
 * Platform abstraction — resolves cross-platform differences
 * between Linux (desktop) and Android (NDK).
 *
 * Provides:
 *   - pthread_barrier_t emulation (missing on Android bionic)
 *   - CPU affinity abstraction
 *   - Audio backend selection
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Platform detection ──────────────────────────────── */
#if defined(__ANDROID__)
#define PS2RE_ANDROID 1
    #define PS2RE_PLATFORM "android"
#elif defined(__linux__)
#define PS2RE_LINUX 1
    #define PS2RE_PLATFORM "linux"
#elif defined(__APPLE__)
#define PS2RE_APPLE 1
    #define PS2RE_PLATFORM "apple"
#else
#define PS2RE_PLATFORM "unknown"
#endif

/* ── pthread_barrier_t emulation (Android bionic) ────── */
#include <pthread.h>

#if defined(PS2RE_ANDROID) || !defined(_POSIX_BARRIERS) || (_POSIX_BARRIERS < 200112L)

typedef struct {
    pthread_mutex_t  mutex;
    pthread_cond_t   cond;
    unsigned         count;
    unsigned         waiting;
    unsigned         generation;
} ps2re_barrier_t;

typedef unsigned ps2re_barrierattr_t;

static inline int ps2re_barrier_init(ps2re_barrier_t* b,
                                     const ps2re_barrierattr_t* attr,
                                     unsigned count)
{
    (void)attr;
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->count      = count;
    b->waiting    = 0;
    b->generation = 0;
    return 0;
}

static inline int ps2re_barrier_destroy(ps2re_barrier_t* b)
{
    pthread_cond_destroy(&b->cond);
    pthread_mutex_destroy(&b->mutex);
    return 0;
}

static inline int ps2re_barrier_wait(ps2re_barrier_t* b)
{
    pthread_mutex_lock(&b->mutex);
    unsigned gen = b->generation;
    b->waiting++;
    if (b->waiting >= b->count) {
        b->generation++;
        b->waiting = 0;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (gen == b->generation) {
            pthread_cond_wait(&b->cond, &b->mutex);
        }
    }
    pthread_mutex_unlock(&b->mutex);
    return 0;
}

#define PS2RE_BARRIER_T        ps2re_barrier_t
#define PS2RE_BARRIERATTR_T    ps2re_barrierattr_t
#define PS2RE_BARRIER_INIT     ps2re_barrier_init
#define PS2RE_BARRIER_DESTROY  ps2re_barrier_destroy
#define PS2RE_BARRIER_WAIT     ps2re_barrier_wait

#else

/* Native pthread_barrier available */
#define PS2RE_BARRIER_T        pthread_barrier_t
#define PS2RE_BARRIERATTR_T    pthread_barrierattr_t
#define PS2RE_BARRIER_INIT(b, a, c)  pthread_barrier_init((b), (a), (c))
#define PS2RE_BARRIER_DESTROY(b)     pthread_barrier_destroy(b)
#define PS2RE_BARRIER_WAIT(b)        pthread_barrier_wait(b)

#endif

/* ── CPU affinity ────────────────────────────────────── */
#include <sched.h>

static inline int ps2re_set_thread_affinity(pthread_t thread, int core_id)
{
#if defined(PS2RE_LINUX) && !defined(PS2RE_ANDROID)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
#elif defined(PS2RE_ANDROID)
    /* Android: use sched_setaffinity from sched.h */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    /* pid_t 0 = current thread; need TID for other threads */
    (void)thread;
    return sched_setaffinity(0, sizeof(cpuset), &cpuset);
#else
    (void)thread;
    (void)core_id;
    return 0;  /* not supported, silently succeed */
#endif
}

/* ── yield ───────────────────────────────────────────── */
static inline void ps2re_yield(void)
{
    sched_yield();
}

/* ── spin wait hint ──────────────────────────────────── */
static inline void ps2re_spin_hint(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("pause" ::: "memory");
#endif
}

#endif /* PS2RE_PLATFORM_H */