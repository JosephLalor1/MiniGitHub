/*
 * git_server.h - the git server
 *
 * A tiny web server on this computer only, speaking git's "smart HTTP"
 * protocol, so git clone, fetch, pull and push work. Pushing (and reading
 * private repositories) needs a Mini GitHub username and password.
 */

#ifndef MINIGITHUB_GIT_SERVER_H
#define MINIGITHUB_GIT_SERVER_H

#include "common.h"
#include "model.h"

/* Starts listening for git on localhost (port 7979, or the next free one). Sets gitPort, or
   gitProblem if it can't. */
void GitServerStart(void);

/* Closes every connection and stops listening. */
void GitServerStop(void);

/* The address git uses for a repository. */
const char *CloneUrl(Repo *repo);

/* Called once per frame: new connections, incoming data, replies going out. */
void GitServerPoll(void);

#endif
