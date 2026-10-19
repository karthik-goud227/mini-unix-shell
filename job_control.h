#ifndef MSH_JOB_CONTROL_H
#define MSH_JOB_CONTROL_H

#include "jobs.h"

/*
 * Returns 0 when foreground handling succeeds, -1 on an
 * infrastructure error. The command's status is stored separately.
 * The caller retains ownership of the Job.
 */
int job_foreground(Job *job, int resume, int *command_status);

#endif
