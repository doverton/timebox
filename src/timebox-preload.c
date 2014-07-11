/*
 * Copyright (c) 2010-2014, David Overton
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>

static time_t timebox_delta;
static int cond_use_monotonic;

#ifdef __APPLE__
static int (*t_gettimeofday)(struct timeval *tv, void *tzp);
#else
static int (*t_gettimeofday)(struct timeval *tv, struct timezone *tzp);
#endif
static int (*t_pthread_cond_timedwait)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
#ifdef HAVE_RT
static int (*t_clock_gettime)(clockid_t, struct timespec *);
static int (*t_pthread_condattr_setpshared)(pthread_condattr_t *, int);
#endif
#ifdef HAVE_GLIBC
static int (*t_pthread_cond_timedwait_2_2_5)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
#endif
static int (*t_pthread_mutex_timedlock)(pthread_mutex_t *, const struct timespec *);
static int (*t_sem_timedwait)(sem_t *, const struct timespec *);
static int (*t_sigtimedwait)(const sigset_t *, siginfo_t *, const struct timespec *);

#ifdef HAVE_GLIBC
__asm__(".symver pthread_cond_timedwait_2_2_5,pthread_cond_timedwait@GLIBC_2.2.5");
#endif

void __attribute__((constructor)) ctor(void)
{
	char *env;
	struct timeval tv;

        *((void **)&t_gettimeofday) = dlsym(RTLD_NEXT, "gettimeofday");
#ifdef HAVE_RT
        *((void **)&t_clock_gettime) = dlsym(RTLD_NEXT, "clock_gettime");
	*((void **)&t_pthread_condattr_setpshared) = dlsym(RTLD_NEXT, "pthread_condattr_setpshared");
#endif
#ifdef HAVE_GLIBC
	*((void **)&t_pthread_cond_timedwait) = dlvsym(RTLD_NEXT, "pthread_cond_timedwait", "GLIBC_2.3.2");
        *((void **)&t_pthread_cond_timedwait_2_2_5) = dlvsym(RTLD_NEXT, "pthread_cond_timedwait", "GLIBC_2.2.5");
#else
	*((void **)&t_pthread_cond_timedwait) = dlsym(RTLD_NEXT, "pthread_cond_timedwait");
#endif
	*((void **)&t_pthread_mutex_timedlock) = dlsym(RTLD_NEXT, "pthread_mutex_timedlock");
	*((void **)&t_sem_timedwait) = dlsym(RTLD_NEXT, "sem_timedwait");
	*((void **)&t_sigtimedwait) = dlsym(RTLD_NEXT, "sigtimedwait");

	if ((env = getenv("TIMEBOX_TIME")) != NULL) {
		char *init = NULL;
		char *endptr = NULL;

		time_t t = strtol(env, &endptr, 0);
		if (*endptr != 0)
			exit(125);

		/* Support consecutive invocations with rolling date. */
		if ((init = getenv("TIMEBOX_INIT")) != NULL) {
			tv.tv_sec = strtol(init, &endptr, 0);
			tv.tv_usec = 0;
			if(*endptr != 0)
				exit(124);
		} else {
			if (gettimeofday(&tv, NULL) < 0)
				exit(123);
		}
		timebox_delta = t - tv.tv_sec;

		/* Be nice and tell people we're here */
		setenv("TIMEBOX_ACTIVE", "yes", 1);
	}

	tzset();
}

#ifdef HAVE_RT
int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	int r;

	if (t_clock_gettime == NULL)
		exit(122);
	if ((r = t_clock_gettime(clk_id, tp)) < 0)
		return r;
	if (clk_id == CLOCK_REALTIME)
		tp->tv_sec += timebox_delta;

	return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared)
{
	int r;
	clockid_t clk_id;

	if (!t_pthread_condattr_setpshared)
		exit(118);
	if ((r = pthread_condattr_setpshared(attr, pshared)) != 0)
		return r;
	if (pthread_condattr_getclock(attr, &clk_id) == 0)
		cond_use_monotonic = (clk_id == CLOCK_MONOTONIC || clk_id == CLOCK_MONOTONIC_RAW);

	return r;
}

#endif

#ifdef __APPLE__
int gettimeofday(struct timeval *tv, void *tzp)
#else
int gettimeofday(struct timeval *tv, struct timezone *tzp)
#endif
{
	int r;

	if (t_gettimeofday == NULL)
		exit(121);
	if ((r = t_gettimeofday(tv, tzp)) < 0)
		return r;

	tv->tv_sec += timebox_delta;

	return 0;
}

time_t time(time_t *t)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0)
		return ((time_t)-1);
	if (t)
		*t = tv.tv_sec;

	return tv.tv_sec;
}

int ftime(struct timeb *tb)
{
	struct timeval tv;
	struct timezone tz = { 0, 0 };

	if (gettimeofday(&tv, &tz) < 0)
		return -1;

	tb->time = tv.tv_sec;
	tb->millitm = tv.tv_usec / 1000;
	tb->timezone = (short)tz.tz_minuteswest;
	tb->dstflag = (short)tz.tz_dsttime;

	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
	struct timespec tp = *abstime;

	if (t_pthread_cond_timedwait == NULL)
		exit(120);

#ifdef HAVE_RT
# ifdef HAVE_GLIBC
	if (cond->__data.__nwaiters & 1 == 0 && cond_use_monotonic == 0)
		tp.tv_sec -= timebox_delta;
#else
	if (!cond_use_monotonic)
		tp.tv_sec -= timebox_delta;
# endif
#endif

	return t_pthread_cond_timedwait(cond, mutex, &tp);
}

#ifdef HAVE_GLIBC
int pthread_cond_timedwait_2_2_5(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
        struct timespec tp = *abstime;

        if (t_pthread_cond_timedwait == NULL)
                exit(120);

# ifdef HAVE_RT
	if (cond->__data.__nwaiters & 1 == 0 && cond_use_monotonic == 0)
	        tp.tv_sec -= timebox_delta;
# endif

        return t_pthread_cond_timedwait_2_2_5(cond, mutex, &tp);
}
#endif

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abs_timeout)
{
	struct timespec tp = *abs_timeout;

	if(t_pthread_mutex_timedlock == NULL)
		exit(119);

	tp.tv_sec -= timebox_delta;
	return t_pthread_mutex_timedlock(mutex, &tp);
}

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
	struct timespec tp = *abs_timeout;

	if(t_sem_timedwait == NULL)
		exit(118);

	tp.tv_sec -= timebox_delta;
	return t_sem_timedwait(sem, &tp);
}

int sigtimedwait(const sigset_t *set, siginfo_t *info, const struct timespec *timeout)
{
	struct timespec tp = *timeout;

	if(t_sigtimedwait == NULL)
		exit(117);

	tp.tv_sec -= timebox_delta;
	return t_sigtimedwait(set, info, &tp);
}

