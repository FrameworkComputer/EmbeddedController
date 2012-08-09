/*
 * Copyright 2003 Sun Microsystems, Inc.
 * Copyright 2010 Google, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Developer's note: This was open sourced by Sun Microsystems, which got it
 * via Cobalt Networks.  It has been fairly extensively modified since then.
 */

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE 1
#endif
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <sched.h>

#include "csem.h"

#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
/* according to X/OPEN we have to define it ourselves */
union semun {
	int val;                    /* value for SETVAL */
	struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;  /* array for GETALL, SETALL */
	struct seminfo *__buf;      /* buffer for IPC_INFO */
};
#endif

/*
 * On some platforms semctl(SETVAL) sets sem_otime, on other platforms it
 * does not.  Figure out what this platform does.
 *
 * Returns 0 if semctl(SETVAL) does not set sem_otime
 * Returns 1 if semctl(SETVAL) does set sem_otime
 * Returns -1 on error
 */
static int does_semctl_set_otime(void)
{
	int sem_id;
	int ret;

	/* create a test semaphore */
	sem_id = semget(IPC_PRIVATE, 1, S_IRUSR|S_IWUSR);
	if (sem_id < 0) {
		return -1;
	}

	/* set the value */
	if (csem_setval(sem_id, 1) < 0) {
		csem_destroy(sem_id);
		return -1;
	}

	/* read sem_otime */
	ret = (csem_get_otime(sem_id) > 0) ? 1 : 0;

	/* clean up */
	csem_destroy(sem_id);

	return ret;
}

int csem_create(key_t key, unsigned val)
{
	static int need_otime_hack = -1;
	int sem_id;

	/* see if we need to trigger a semop to set sem_otime */
	if (need_otime_hack < 0) {
		int ret = does_semctl_set_otime();
		if (ret < 0) {
			return -1;
		}
		need_otime_hack = !ret;
	}

	/* create it or fail */
	sem_id = semget(key, 1, IPC_CREAT|IPC_EXCL | S_IRUSR|S_IWUSR);
	if (sem_id < 0) {
		return -1;
	}

	/* initalize the value */
	if (need_otime_hack) {
		val++;
	}
	if (csem_setval(sem_id, val) < 0) {
		csem_destroy(sem_id);
		return -1;
	}

	if (need_otime_hack) {
		/* force sem_otime to change */
		csem_down(sem_id);
	}

	return sem_id;
}

/* how many times to loop, waiting for sem_otime */
#define MAX_OTIME_LOOPS 1000

int csem_get(key_t key)
{
	int sem_id;
	int i;

	/* CSEM_PRIVATE needs to go through csem_create() to get an
	 * initial value */
	if (key == CSEM_PRIVATE) {
		errno = EINVAL;
		return -1;
	}

	/* get the (assumed existing) semaphore */
	sem_id = semget(key, 1, S_IRUSR|S_IWUSR);
	if (sem_id < 0) {
		return -1;
	}

	/* loop until sem_otime != 0, which means it has been initialized */
	for (i = 0; i < MAX_OTIME_LOOPS; i++) {
		time_t otime = csem_get_otime(sem_id);
		if (otime < 0) {
			/* error */
			return -1;
		}
		if (otime > 0) {
			/* success */
			return sem_id;
		}
		/* retry */
		sched_yield();
	}

	/* fell through - error */
	return -1;
}

int csem_get_or_create(key_t key, unsigned val)
{
	int sem_id;

	/* try to create the semaphore */
	sem_id = csem_create(key, val);
	if (sem_id >= 0 || errno != EEXIST) {
		/* it either succeeded or got an error */
		return sem_id;
	}

	/* it must exist already - get it */
	sem_id = csem_get(key);
	if (sem_id < 0) {
		return -1;
	}

	return sem_id;
}

int csem_destroy(int sem_id)
{
	return semctl(sem_id, 0, IPC_RMID);
}

int csem_getval(int sem_id)
{
	return semctl(sem_id, 0, GETVAL);
}

int csem_setval(int sem_id, unsigned val)
{
	union semun arg;
	arg.val = val;
	if (semctl(sem_id, 0, SETVAL, arg) < 0) {
		return -1;
	}
	return 0;
}

static int csem_up_undoflag(int sem_id, int undoflag)
{
	struct sembuf sops;
	sops.sem_num = 0;
	sops.sem_op = 1;
	sops.sem_flg = undoflag;
	return semop(sem_id, &sops, 1);
}

int csem_up(int sem_id)
{
	return csem_up_undoflag(sem_id, 0);
}

int csem_up_undo(int sem_id)
{
	return csem_up_undoflag(sem_id, SEM_UNDO);
}

static int csem_down_undoflag(int sem_id, int undoflag)
{
	struct sembuf sops;
	sops.sem_num = 0;
	sops.sem_op = -1;
	sops.sem_flg = undoflag;
	return semop(sem_id, &sops, 1);
}

int csem_down(int sem_id)
{
	return csem_down_undoflag(sem_id, 0);
}

int csem_down_undo(int sem_id)
{
	return csem_down_undoflag(sem_id, SEM_UNDO);
}

static int csem_down_timeout_undoflag(int sem_id,
                                      struct timespec *timeout,
                                      int undoflag)
{
	struct sembuf sops;
	sops.sem_num = 0;
	sops.sem_op = -1;
	sops.sem_flg = undoflag;
	return semtimedop(sem_id, &sops, 1, timeout);
}

int csem_down_timeout(int sem_id, struct timespec *timeout)
{
	return csem_down_timeout_undoflag(sem_id, timeout, 0);
}

int csem_down_timeout_undo(int sem_id, struct timespec *timeout)
{
	return csem_down_timeout_undoflag(sem_id, timeout, SEM_UNDO);
}

time_t csem_get_otime(int sem_id)
{
	union semun arg;
	struct semid_ds ds;
	arg.buf = &ds;
	if (semctl(sem_id, 0, IPC_STAT, arg) < 0) {
		return -1;
	}
	return ds.sem_otime;
}
