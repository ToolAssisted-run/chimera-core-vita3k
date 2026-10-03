// ThreadTest: the scheduler made visible (CC0).
//
// Three workers loop on a kernel mutex, each doing a different amount of
// arithmetic between turns; every turn is credited to its thread. Each frame
// the main thread draws, through GXM (vita2d), one bar per worker as long as
// its share of the turns, a strip of the last 96 turn owners, a square that
// moves with the process clock, and a background that cycles with the frame
// count. Any change in how the threads interleave or how time passes changes
// the picture.
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

#define WORKERS 3
#define HISTORY 96

static SceUID mutex;
static volatile unsigned int turns[WORKERS];
static volatile unsigned char history[HISTORY];
static volatile unsigned int history_pos;
static volatile unsigned int total;

static int worker(SceSize args, void *argp) {
    const int id = *(int *)argp;
    unsigned int x = 12345u + (unsigned)id;
    for (;;) {
        // a different amount of work for each thread, varying turn to turn
        const unsigned int spins = 2000u + (unsigned)id * 1500u + (x & 1023u);
        for (unsigned int i = 0; i < spins; i++)
            x = x * 1664525u + 1013904223u;
        sceKernelLockMutex(mutex, 1, NULL);
        turns[id]++;
        history[history_pos % HISTORY] = (unsigned char)id;
        history_pos++;
        total++;
        sceKernelUnlockMutex(mutex, 1);
        if ((x >> 28) == 0)
            sceKernelDelayThread(100 + (x & 511u));
    }
    return 0;
}

int main(int argc, char *argv[]) {
    static const unsigned int colours[WORKERS] = { 0xFF3030E0u, 0xFF30E030u, 0xFFE03030u };
    vita2d_init();
    mutex = sceKernelCreateMutex("threadtest", 0, 0, NULL);
    static int ids[WORKERS];
    for (int i = 0; i < WORKERS; i++) {
        ids[i] = i;
        SceUID th = sceKernelCreateThread("worker", worker, 0x10000100, 0x10000, 0, 0, NULL);
        sceKernelStartThread(th, sizeof(int), &ids[i]);
    }

    for (unsigned int frame = 0;; frame++) {
        vita2d_set_clear_color(RGBA8((frame * 3) & 255, (frame * 5) & 255, (frame * 7) & 255, 255));
        vita2d_start_drawing();
        vita2d_clear_screen();

        sceKernelLockMutex(mutex, 1, NULL);
        const unsigned int all = total ? total : 1;
        for (int i = 0; i < WORKERS; i++) {
            const float len = 900.0f * (float)turns[i] / (float)all;
            vita2d_draw_rectangle(30, 40 + i * 70, len, 50, colours[i]);
        }
        for (int i = 0; i < HISTORY; i++) {
            const unsigned char owner = history[(history_pos + (unsigned)i) % HISTORY];
            vita2d_draw_rectangle(30 + i * 9, 270, 8, 40, colours[owner % WORKERS]);
        }
        sceKernelUnlockMutex(mutex, 1);

        // the process clock, in machine time
        const SceUInt64 us = sceKernelGetProcessTimeWide();
        vita2d_draw_rectangle((float)((us / 2000u) % 900u), 350, 60, 60, 0xFFFFFFFFu);
        vita2d_draw_fill_circle(480, 470, 20 + (frame % 40), 0xFF00FFFFu);

        vita2d_end_drawing();
        vita2d_swap_buffers();
    }
    return 0;
}
