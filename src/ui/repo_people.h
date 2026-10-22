/*
 * repo_people.h - the People tab
 *
 * Who has access, invitations, and the pie chart of who did how much.
 */

#ifndef MINIGITHUB_REPO_PEOPLE_H
#define MINIGITHUB_REPO_PEOPLE_H

#include "common.h"
#include "model.h"

/* The little pie in a repository's header. Returns true if it was clicked. */
bool HeaderPie(Repo *repo, float rightX, float centerY);

/* Draws a repository's People tab. */
void TabPeople(Repo *repo);

#endif
