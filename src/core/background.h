/*
 * background.h - running a slow job on another thread
 *
 * Loading and saving a big save file take long enough to freeze the window
 * for a moment, so they run on a second thread while the window keeps
 * drawing. One job per BackgroundTask; the main thread checks on it each
 * frame, or waits for it when it has to.
 */

#ifndef MINIGITHUB_BACKGROUND_H
#define MINIGITHUB_BACKGROUND_H

#include "common.h"

/* One job running on another thread. All zero = nothing running. */
typedef struct { void *thread; int running; int finished; void (*work)(void *); void *argument; } BackgroundTask;

/* Starts work(argument) on another thread. If no thread can be started, it runs
   right here instead (so it always happens). Only one job per task at a time. */
void RunInBackground(BackgroundTask *task, void (*work)(void *), void *argument);

/* Is a job started on this task still going? */
bool BackgroundBusy(BackgroundTask *task);

/* Waits for the job (if any) to finish, and tidies up its thread. */
void WaitForBackground(BackgroundTask *task);

#endif
