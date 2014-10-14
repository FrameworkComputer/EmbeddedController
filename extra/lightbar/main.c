/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#include "simulation.h"

static void *(*thread_fns[])(void *) = {
	entry_windows,
	entry_lightbar,
	entry_input,
};

int main(int argc, char *argv[])
{
	int i;
	pthread_t thread[ARRAY_SIZE(thread_fns)];

	printf("\nLook at the README file.\n");
	printf("Click in the window.\n");
	printf("Type \"help\" for commands.\n\n");
	fflush(stdout);

	init_windows();

	for (i = 0; i < ARRAY_SIZE(thread_fns); i++)
		assert(0 == pthread_create(&thread[i], NULL, thread_fns[i], 0));

	for (i = 0; i < ARRAY_SIZE(thread_fns); i++)
		pthread_join(thread[i], NULL);

	return 0;
}

void *entry_lightbar(void *ptr)
{
	lightbar_task();
	return 0;
}

/****************************************************************************/
/* Fake functions. We only have to implement enough for lightbar.c */

/* timespec uses nanoseconds */
#define TS_USEC 1000L
#define TS_MSEC 1000000L
#define TS_SEC  1000000000L

static void timespec_incr(struct timespec *v, time_t secs, long nsecs)
{
	v->tv_sec += secs;
	/* The nanosecond sum won't overflow, but might have a carry. */
	v->tv_nsec += nsecs;
	v->tv_sec += v->tv_nsec / TS_SEC;
	v->tv_nsec %= TS_SEC;
}


static pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t task_cond = PTHREAD_COND_INITIALIZER;
static uint32_t task_event;

uint32_t task_wait_event(int timeout_us)
{
	struct timespec t;
	uint32_t event;

	pthread_mutex_lock(&task_mutex);

	if (timeout_us > 0) {
		clock_gettime(CLOCK_REALTIME, &t);
		timespec_incr(&t, timeout_us / SECOND, timeout_us * TS_USEC);

		if (ETIMEDOUT == pthread_cond_timedwait(&task_cond,
							&task_mutex, &t))
			task_event |= TASK_EVENT_TIMER;
	} else {
		pthread_cond_wait(&task_cond, &task_mutex);
	}

	pthread_mutex_unlock(&task_mutex);
	event = task_event;
	task_event = 0;
	return event;
}

uint32_t task_set_event(task_id_t tskid,	/* always LIGHTBAR */
			uint32_t event,
			int wait_for_reply)	/* always 0 */
{
	pthread_mutex_lock(&task_mutex);
	task_event = event;
	pthread_cond_signal(&task_cond);
	pthread_mutex_unlock(&task_mutex);
	return 0;
}



/* Stubbed functions */

void cprintf(int zero, const char *fmt, ...)
{
	va_list ap;
	char *s;
	char *newfmt = strdup(fmt);

	for (s = newfmt; *s; s++)
		if (*s == '%' && s[1] == 'T')
			*s = 'T';

	va_start(ap, fmt);
	vprintf(newfmt, ap);
	va_end(ap);

	free(newfmt);
}

void cprints(int zero, const char *fmt, ...)
{
	va_list ap;

	printf("[TT ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("]\n");
}

timestamp_t get_time(void)
{
	static struct timespec t_start;
	struct timespec t;
	timestamp_t ret;

	if (!t_start.tv_sec)
		clock_gettime(CLOCK_REALTIME, &t_start);
	clock_gettime(CLOCK_REALTIME, &t);
	ret.val = (t.tv_sec - t_start.tv_sec) * SECOND +
		(t.tv_nsec - t_start.tv_nsec) / TS_USEC;
	return ret;
}

/* We could implement these if we wanted to test their usage. */
int system_add_jump_tag(uint16_t tag, int version, int size, const void *data)
{
	return 0;
}

uint8_t *system_get_jump_tag(uint16_t tag, int *version, int *size)
{
	return 0;
}

/* Copied from util/ectool.c */
int lb_read_params_from_file(const char *filename,
			     struct lightbar_params_v1 *p)
{
	FILE *fp;
	char buf[80];
	int val[4];
	int r = 1;
	int line = 0;
	int want, got;
	int i;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	/* We must read the correct number of params from each line */
#define READ(N) do {							\
		line++;							\
		want = (N);						\
		got = -1;						\
		if (!fgets(buf, sizeof(buf), fp))			\
			goto done;					\
		got = sscanf(buf, "%i %i %i %i",			\
			     &val[0], &val[1], &val[2], &val[3]);	\
		if (want != got)					\
			goto done;					\
	} while (0)


	/* Do it */
	READ(1); p->google_ramp_up = val[0];
	READ(1); p->google_ramp_down = val[0];
	READ(1); p->s3s0_ramp_up = val[0];
	READ(1); p->s0_tick_delay[0] = val[0];
	READ(1); p->s0_tick_delay[1] = val[0];
	READ(1); p->s0a_tick_delay[0] = val[0];
	READ(1); p->s0a_tick_delay[1] = val[0];
	READ(1); p->s0s3_ramp_down = val[0];
	READ(1); p->s3_sleep_for = val[0];
	READ(1); p->s3_ramp_up = val[0];
	READ(1); p->s3_ramp_down = val[0];
	READ(1); p->tap_tick_delay = val[0];
	READ(1); p->tap_gate_delay = val[0];
	READ(1); p->tap_display_time = val[0];

	READ(1); p->tap_pct_red = val[0];
	READ(1); p->tap_pct_green = val[0];
	READ(1); p->tap_seg_min_on = val[0];
	READ(1); p->tap_seg_max_on = val[0];
	READ(1); p->tap_seg_osc = val[0];
	READ(3);
	p->tap_idx[0] = val[0];
	p->tap_idx[1] = val[1];
	p->tap_idx[2] = val[2];

	READ(2);
	p->osc_min[0] = val[0];
	p->osc_min[1] = val[1];
	READ(2);
	p->osc_max[0] = val[0];
	p->osc_max[1] = val[1];
	READ(2);
	p->w_ofs[0] = val[0];
	p->w_ofs[1] = val[1];

	READ(2);
	p->bright_bl_off_fixed[0] = val[0];
	p->bright_bl_off_fixed[1] = val[1];

	READ(2);
	p->bright_bl_on_min[0] = val[0];
	p->bright_bl_on_min[1] = val[1];

	READ(2);
	p->bright_bl_on_max[0] = val[0];
	p->bright_bl_on_max[1] = val[1];

	READ(3);
	p->battery_threshold[0] = val[0];
	p->battery_threshold[1] = val[1];
	p->battery_threshold[2] = val[2];

	READ(4);
	p->s0_idx[0][0] = val[0];
	p->s0_idx[0][1] = val[1];
	p->s0_idx[0][2] = val[2];
	p->s0_idx[0][3] = val[3];

	READ(4);
	p->s0_idx[1][0] = val[0];
	p->s0_idx[1][1] = val[1];
	p->s0_idx[1][2] = val[2];
	p->s0_idx[1][3] = val[3];

	READ(4);
	p->s3_idx[0][0] = val[0];
	p->s3_idx[0][1] = val[1];
	p->s3_idx[0][2] = val[2];
	p->s3_idx[0][3] = val[3];

	READ(4);
	p->s3_idx[1][0] = val[0];
	p->s3_idx[1][1] = val[1];
	p->s3_idx[1][2] = val[2];
	p->s3_idx[1][3] = val[3];

	for (i = 0; i < ARRAY_SIZE(p->color); i++) {
		READ(3);
		p->color[i].r = val[0];
		p->color[i].g = val[1];
		p->color[i].b = val[2];
	}

#undef READ

	/* Yay */
	r = 0;
done:
	if (r)
		fprintf(stderr, "problem with line %d: wanted %d, got %d\n",
			line, want, got);
	fclose(fp);
	return r;
}

int lb_load_program(const char *filename, struct lightbar_program *prog)
{
	FILE *fp;
	size_t got;
	int rc;

	fp = fopen(filename, "rb");
	if (!fp) {
		fprintf(stderr, "Can't open %s: %s\n",
			filename, strerror(errno));
		return 1;
	}

	rc = fseek(fp, 0, SEEK_END);
	if (rc) {
		fprintf(stderr, "Couldn't find end of file %s",
				filename);
		fclose(fp);
		return 1;
	}
	rc = (int) ftell(fp);
	if (rc > EC_LB_PROG_LEN) {
		fprintf(stderr, "File %s is too long, aborting\n", filename);
		fclose(fp);
		return 1;
	}
	rewind(fp);

	memset(prog->data, 0, EC_LB_PROG_LEN);
	got = fread(prog->data, 1, EC_LB_PROG_LEN, fp);
	if (rc != got)
		fprintf(stderr, "Warning: did not read entire file\n");
	prog->size = got;
	fclose(fp);
	return 0;
}
