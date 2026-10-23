/*
 * background.c - running a slow job on another thread
 *
 * A thin layer over the system's threads: _beginthreadex on Windows,
 * pthreads elsewhere. The job sets "finished" when it's done (read and
 * written with GCC's atomic built-ins, so the other thread sees it).
 */

#include "background.h"

#ifdef _WIN32
#include <process.h>
/* (declared here rather than by including windows.h, whose names clash with raylib's) */
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void *handle, unsigned long milliseconds);
__declspec(dllimport) int __stdcall CloseHandle(void *handle);
#define WAIT_FOREVER 0xFFFFFFFFul
#else
#include <pthread.h>
#endif

/* Runs the job, then says it's finished. */
static void RunJob(BackgroundTask *task)
{
    task->work(task->argument);
    __atomic_store_n(&task->finished, 1, __ATOMIC_RELEASE);
}

#ifdef _WIN32
static unsigned __stdcall ThreadStart(void *task) { RunJob(task); return 0; }
#else
static void *ThreadStart(void *task) { RunJob(task); return NULL; }
#endif

void RunInBackground(BackgroundTask *task, void (*work)(void *), void *argument)
{
    WaitForBackground(task);                               /* one job at a time */
    task->work = work; task->argument = argument;
    task->finished = 0; task->running = 1;
#ifdef _WIN32
    task->thread = (void *)_beginthreadex(NULL, 0, ThreadStart, task, 0, NULL);
#else
    pthread_t *thread = malloc(sizeof *thread);
    if (thread && pthread_create(thread, NULL, ThreadStart, task) != 0) { free(thread); thread = NULL; }
    task->thread = thread;
#endif
    if (!task->thread) RunJob(task);                       /* no thread to be had: do it now */
}

bool BackgroundBusy(BackgroundTask *task)
{
    return task->running && !__atomic_load_n(&task->finished, __ATOMIC_ACQUIRE);
}

void WaitForBackground(BackgroundTask *task)
{
    if (!task->running) return;
    if (task->thread) {
#ifdef _WIN32
        WaitForSingleObject(task->thread, WAIT_FOREVER);
        CloseHandle(task->thread);
#else
        pthread_join(*(pthread_t *)task->thread, NULL);
        free(task->thread);
#endif
    }
    task->thread = NULL;
    task->running = 0;
}
