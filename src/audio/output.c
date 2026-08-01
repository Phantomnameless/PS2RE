#include "ps2re/types.h"
#include "ps2re/config.h"
#include "ps2re/platform.h"
#include "ps2re/async/ring_buffer.h"
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <arm_neon.h>

/*
 * Audio output — multiplatform:
 *   Linux: ALSA
 *   Android: AAudio / stub
 */

#define AUDIO_SAMPLE_RATE    48000
#define AUDIO_CHANNELS       2
#define AUDIO_BUFFER_SAMPLES 1024
#define AUDIO_RING_FRAMES    8

typedef struct {
    f32 samples[AUDIO_BUFFER_SAMPLES * AUDIO_CHANNELS];
} AudioFrame;

extern void mixer_mix(void* mixer, f32* output, int frames);

typedef struct AudioOutput {
    SPSCRing       ring;
    void*          mixer;
    pthread_t      thread;
    _Atomic(int)   running;        /* ← bool→int */
    void*          platform_handle;
    u64            frames_output;
    f32            buffer_ms;
} AudioOutput;

/* ── Platform backends ───────────────────────────────── */

#if defined(PS2RE_LINUX) && !defined(PS2RE_ANDROID)
#include <alsa/asoundlib.h>

static Result audio_platform_open(void** handle, u32 rate, u32 channels,
                                  u32 buffer_samples)
{
    snd_pcm_t* pcm;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "ALSA open failed: %s\n", snd_strerror(err));
        return ERR_IO;
    }

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, channels);

    unsigned int r = rate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &r, 0);

    snd_pcm_uframes_t buf_sz = buffer_samples * 4;
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buf_sz);

    err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) {
        snd_pcm_close(pcm);
        return ERR_IO;
    }

    *handle = pcm;
    return OK;
}

static void audio_platform_write(void* handle, const s16* data, u32 frames)
{
    snd_pcm_t* pcm = (snd_pcm_t*)handle;
    snd_pcm_sframes_t written = snd_pcm_writei(pcm, data, (snd_pcm_uframes_t)frames);
    if (written < 0) {
        snd_pcm_recover(pcm, (int)written, 1);
    }
}

static void audio_platform_close(void* handle)
{
    if (handle) {
        snd_pcm_drain((snd_pcm_t*)handle);
        snd_pcm_close((snd_pcm_t*)handle);
    }
}

#elif defined(PS2RE_ANDROID)
/* Android: AAudio would go here. For now, stub implementation. */
#include <aaudio/AAudio.h>   /* ← se disponível no NDK */

static AAudioStream* g_aaudio_stream = NULL;

static Result audio_platform_open(void** handle, u32 rate, u32 channels,
                                  u32 buffer_samples)
{
    (void)handle;
    AAudioStreamBuilder* builder = NULL;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setPerformanceMode(builder,
        AAudioPerformanceMode_LowLatency);
    AAudioStreamBuilder_setDirection(builder, AAudioDirection_Output);
    AAudioStreamBuilder_setSampleRate(builder, (int)rate);
    AAudioStreamBuilder_setChannelCount(builder, (int)channels);
    AAudioStreamBuilder_setFormat(builder, AAudioFormat_PCM_I16);

    aaudio_result_t result = AAudioStreamBuilder_openStream(builder,
                                                            &g_aaudio_stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK) return ERR_IO;
    AAudioStream_requestStart(g_aaudio_stream);
    *handle = NULL;
    return OK;
}

static void audio_platform_write(void* handle, const s16* data, u32 frames)
{
    (void)handle;
    if (g_aaudio_stream) {
        AAudioStream_write(g_aaudio_stream, data, (int32_t)frames, 100000000);
    }
}

static void audio_platform_close(void* handle)
{
    (void)handle;
    if (g_aaudio_stream) {
        AAudioStream_requestStop(g_aaudio_stream);
        AAudioStream_close(g_aaudio_stream);
        g_aaudio_stream = NULL;
    }
}

#else
/* Fallback stub */
static Result audio_platform_open(void** h, u32 r, u32 c, u32 b)
{
    (void)h; (void)r; (void)c; (void)b;
    return OK;
}
static void audio_platform_write(void* h, const s16* d, u32 f)
{
    (void)h; (void)d; (void)f;
}
static void audio_platform_close(void* h) { (void)h; }
#endif

/* ── Float → Int16 NEON ──────────────────────────────── */

static void float_to_s16_neon(s16* dst, const f32* src, int sample_count)
{
    float32x4_t scale = vdupq_n_f32(32767.0f);
    float32x4_t lo    = vdupq_n_f32(-32768.0f);
    float32x4_t hi    = vdupq_n_f32(32767.0f);

    int i = 0;
    for (; i + 8 <= sample_count; i += 8) {
        PREFETCH_R(src + i + 32);

        float32x4_t f0 = vld1q_f32(src + i);
        float32x4_t f1 = vld1q_f32(src + i + 4);

        f0 = vmaxq_f32(f0, lo);
        f0 = vminq_f32(f0, hi);
        f1 = vmaxq_f32(f1, lo);
        f1 = vminq_f32(f1, hi);

        f0 = vmulq_f32(f0, scale);
        f1 = vmulq_f32(f1, scale);

        int32x4_t i0 = vcvtq_s32_f32(f0);
        int32x4_t i1 = vcvtq_s32_f32(f1);

        int16x4_t n0 = vmovn_s32(i0);
        int16x4_t n1 = vmovn_s32(i1);
        int16x8_t combined = vcombine_s16(n0, n1);

        vst1q_s16(dst + i, combined);
    }

    for (; i < sample_count; i++) {
        f32 s = src[i];
        if (s > 1.0f)  s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        dst[i] = (s16)(s * 32767.0f);
    }
}

/* ── Output Thread ───────────────────────────────────── */

static void* audio_output_thread(void* arg)
{
    AudioOutput* ao = (AudioOutput*)arg;
    s16 int16_buf[AUDIO_BUFFER_SAMPLES * AUDIO_CHANNELS];

    while (atomic_load_explicit(&ao->running, memory_order_acquire)) {
        AudioFrame frame;
        if (spsc_ring_pop(&ao->ring, &frame) != OK) {
            memset(int16_buf, 0, sizeof(int16_buf));
            audio_platform_write(ao->platform_handle, int16_buf,
                                 AUDIO_BUFFER_SAMPLES);
            continue;
        }

        float_to_s16_neon(int16_buf, frame.samples,
                          AUDIO_BUFFER_SAMPLES * AUDIO_CHANNELS);
        audio_platform_write(ao->platform_handle, int16_buf,
                             AUDIO_BUFFER_SAMPLES);
        ao->frames_output += AUDIO_BUFFER_SAMPLES;
    }
    return NULL;
}

/* ── Public API ──────────────────────────────────────── */

Result audio_output_init(AudioOutput* ao, void* mixer)
{
    memset(ao, 0, sizeof(*ao));
    ao->mixer = mixer;

    TRY(spsc_ring_init(&ao->ring, AUDIO_RING_FRAMES, sizeof(AudioFrame)));
    TRY(audio_platform_open(&ao->platform_handle, AUDIO_SAMPLE_RATE,
                            AUDIO_CHANNELS, AUDIO_BUFFER_SAMPLES));

    ao->buffer_ms = (f32)AUDIO_BUFFER_SAMPLES / (f32)AUDIO_SAMPLE_RATE * 1000.0f;

    atomic_init(&ao->running, 1);
    pthread_create(&ao->thread, NULL, audio_output_thread, ao);
    ps2re_set_thread_affinity(ao->thread, 4);  /* ← */

    printf("audio: initialized @ %dHz, %.1fms buffer\n",
           AUDIO_SAMPLE_RATE, ao->buffer_ms);
    return OK;
}

void audio_output_destroy(AudioOutput* ao)
{
    atomic_store_explicit(&ao->running, 0, memory_order_release);
    pthread_join(ao->thread, NULL);
    audio_platform_close(ao->platform_handle);
    spsc_ring_destroy(&ao->ring);
}

void audio_output_submit(AudioOutput* ao)
{
    AudioFrame frame;
    mixer_mix(ao->mixer, frame.samples, AUDIO_BUFFER_SAMPLES);
    spsc_ring_push(&ao->ring, &frame);
}