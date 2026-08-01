#include "ps2re/types.h"
#include "ps2re/config.h"
#include "ps2re/sched/frame_scheduler.h"
#include "ps2re/render/renderer.h"
#include "ps2re/input/pad.h"
#include <stdio.h>
#include <time.h>

static f64 get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (f64)ts.tv_sec + (f64)ts.tv_nsec * 1e-9;
}

int main(int argc, char* argv[])    /* ← char* argv[] (sem const) */
{
    (void)argc;
    (void)argv;

    printf("ps2re: PS2->ARM64 re-architecture [%s]\n", PS2RE_PLATFORM);

    Renderer renderer;
    Result r = renderer_init(&renderer, NULL);
    if (r != OK) {
        fprintf(stderr, "Failed to init renderer: %d\n", r);
        return 1;
    }

    FrameScheduler sched;
    r = frame_scheduler_init(&sched, &renderer);
    if (r != OK) {
        fprintf(stderr, "Failed to init scheduler: %d\n", r);
        renderer_destroy(&renderer);
        return 1;
    }

    f64 last_time = get_time();
    f64 fps_timer = 0.0;
    int fps_count = 0;

    while (!frame_scheduler_should_quit(&sched)) {
        f64 now = get_time();
        f64 dt  = now - last_time;
        last_time = now;

        frame_scheduler_run_frame(&sched, dt);

        fps_timer += dt;
        fps_count++;
        if (fps_timer >= 1.0) {
            printf("FPS: %d\n", fps_count);
            fps_timer -= 1.0;
            fps_count = 0;
        }
    }

    renderer_wait_idle(&renderer);
    frame_scheduler_destroy(&sched);
    renderer_destroy(&renderer);

    printf("ps2re: shutdown clean\n");
    return 0;
}