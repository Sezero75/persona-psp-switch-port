#include <switch.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*RowFunction)(void *, unsigned, unsigned);
typedef struct {
    Thread thread;
    Semaphore start, done;
    RowFunction draw;
    void *argument;
    unsigned index, lanes;
} RenderWorker;

static RenderWorker workers[2];
static unsigned count;
static bool initialized;

static void render_worker(void *arg) {
    RenderWorker *w = arg;
    for (;;) {
        semaphoreWait(&w->start);
        if (!w->draw) return;
        w->draw(w->argument, w->index, w->lanes);
        semaphoreSignal(&w->done);
    }
}

static void shutdown_workers(void) {
    for (unsigned i = 0; i < count; ++i) {
        workers[i].draw = NULL;
        semaphoreSignal(&workers[i].start);
        threadWaitForExit(&workers[i].thread);
        threadClose(&workers[i].thread);
    }
    count = 0;
}

static void start_workers(void) {
    initialized = true;
    const unsigned current = svcGetCurrentProcessorNumber();
    for (int core = 0; core < 3 && count < 2; ++core) {
        if ((unsigned)core == current) continue;
        RenderWorker *w = &workers[count];
        semaphoreInit(&w->start, 0);
        semaphoreInit(&w->done, 0);
        w->index = count + 1;
        if (R_FAILED(threadCreate(&w->thread, render_worker, w, NULL, 0x10000, 0x2c, core))) continue;
        if (R_FAILED(threadStart(&w->thread))) { threadClose(&w->thread); continue; }
        ++count;
    }
    if (count) atexit(shutdown_workers);
    fprintf(stderr, "Renderer using %u CPU lanes\n", count + 1);
}

/* The GE runs one primitive at a time. Both semaphore handoffs complete before
 * its state, palette or job storage can be reused by the next draw. */
unsigned nx_render_stripes(RowFunction draw, void *argument) {
    if (!initialized) start_workers();
    unsigned lanes = count + 1;
    for (unsigned i = 0; i < count; ++i) {
        workers[i].draw = draw;
        workers[i].argument = argument;
        workers[i].lanes = lanes;
        semaphoreSignal(&workers[i].start);
    }
    draw(argument, 0, lanes);
    for (unsigned i = 0; i < count; ++i) semaphoreWait(&workers[i].done);
    return lanes;
}
