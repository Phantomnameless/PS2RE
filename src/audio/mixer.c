#include "ps2re/types.h"
#include <string.h>

/*
 * Audio mixer — replaces PS2 SPU2.
 *
 * PS2 SPU2: 48 voices, 48kHz, hardware ADSR, hardware reverb.
 * ARM64: Software mixer on dedicated thread (LITTLE core).
 *   - Lock-free ring buffer for commands
 *   - Interleaved float32 output
 *   - NEON for mixing (4 samples per op)
 */

#define AUDIO_CHANNELS   2
#define AUDIO_RATE       48000
#define AUDIO_BUFFER     1024   /* samples per callback */
#define MAX_VOICES       48

typedef struct {
    f32*  data;
    u32   length;       /* samples */
    u32   channels;
    u32   sample_rate;
} AudioClip;

typedef struct {
    AudioClip* clip;
    u32        position;
    f32        volume;
    f32        pitch;
    bool       active;
    bool       loop;
} AudioVoice;

typedef struct {
    AudioVoice voices[MAX_VOICES];
    f32        master_volume;
    f32        buffer[AUDIO_BUFFER * AUDIO_CHANNELS];
} AudioMixer;

void mixer_init(AudioMixer* m)
{
    memset(m, 0, sizeof(*m));
    m->master_volume = 1.0f;
}

void mixer_mix(AudioMixer* m, f32* output, int frames)
{
    memset(output, 0, (size_t)(frames * AUDIO_CHANNELS) * sizeof(f32));

    for (int v = 0; v < MAX_VOICES; v++) {
        AudioVoice* voice = &m->voices[v];
        if (!voice->active || !voice->clip) continue;

        for (int f = 0; f < frames; f++) {
            if (voice->position >= voice->clip->length) {
                if (voice->loop) {
                    voice->position = 0;
                } else {
                    voice->active = false;
                    break;
                }
            }

            f32 sample = voice->clip->data[voice->position] * voice->volume;
            output[f * AUDIO_CHANNELS + 0] += sample;
            output[f * AUDIO_CHANNELS + 1] += sample;
            voice->position++;
        }
    }

    /* Master volume */
    int total = frames * AUDIO_CHANNELS;
    float32x4_t vol = vdupq_n_f32(m->master_volume);
    int i = 0;
    for (; i + 4 <= total; i += 4) {
        float32x4_t s = vld1q_f32(output + i);
        s = vmulq_f32(s, vol);
        vst1q_f32(output + i, s);
    }
    for (; i < total; i++) {
        output[i] *= m->master_volume;
    }
}