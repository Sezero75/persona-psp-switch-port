#pragma once
#include <switch.h>
#include <pthread.h>
#include <stdlib.h>

#define SDLCALL
typedef Mutex SDL_Mutex;
typedef CondVar SDL_Condition;
typedef pthread_t SDL_Thread;

typedef struct {
    int (*run)(void *);
    void *argument;
} NxSchedulerJob;

static inline SDL_Mutex *SDL_CreateMutex(void) { return calloc(1, sizeof(Mutex)); }
static inline SDL_Condition *SDL_CreateCondition(void) { return calloc(1, sizeof(CondVar)); }
#define SDL_LockMutex mutexLock
#define SDL_UnlockMutex mutexUnlock
#define SDL_SignalCondition condvarWakeOne
#define SDL_DestroyCondition free
#define SDL_DestroyMutex free
#define SDL_WaitCondition condvarWait

static inline bool SDL_WaitConditionTimeout(SDL_Condition *condition, SDL_Mutex *mutex, int milliseconds) {
    if (milliseconds < 0) return R_SUCCEEDED(condvarWait(condition, mutex));
    return R_SUCCEEDED(condvarWaitTimeout(condition, mutex, (uint64_t)milliseconds * 1000000u));
}
static inline uint64_t SDL_GetTicksNS(void) { return armTicksToNs(armGetSystemTick()); }
#define SDL_DelayPrecise svcSleepThread
#define SDL_DelayNS svcSleepThread

static inline void *nx_scheduler_worker(void *pointer) {
    NxSchedulerJob job = *(NxSchedulerJob *)pointer;
    free(pointer);
    job.run(job.argument);
    return NULL;
}
static inline SDL_Thread *SDL_CreateThread(int (*run)(void *), const char *name, void *argument) {
    (void)name;
    SDL_Thread *thread = malloc(sizeof(*thread));
    NxSchedulerJob *job = malloc(sizeof(*job));
    if (thread && job) {
        job->run = run;
        job->argument = argument;
        if (!pthread_create(thread, NULL, nx_scheduler_worker, job)) return thread;
    }
    free(thread);
    free(job);
    return NULL;
}
static inline void SDL_DetachThread(SDL_Thread *thread) {
    pthread_detach(*thread);
    free(thread);
}
#define SDL_THREAD_PRIORITY_HIGH 0x2b
static inline bool SDL_SetCurrentThreadPriority(int priority) {
    return R_SUCCEEDED(svcSetThreadPriority(CUR_THREAD_HANDLE, priority));
}
static inline const char *SDL_GetError(void) { return "libnx thread service unavailable"; }
static inline const char *SDL_GetCurrentVideoDriver(void) { return "libnx"; }
