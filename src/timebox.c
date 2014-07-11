/*
 * Copyright (c) 2010-2012, David Overton
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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

static const char *get_preloader(const char *progname);

int main(int argc, char *argv[])
{
	time_t test;
	char *errptr;
	char **sub_argv;
	int sub_argc;
	int r;
	char filebuf[32];
	char startenv[32];

	char *sub_shell[] = {
		"/bin/bash",
		"-i",
		NULL
	};

	if (argc < 2) {
		fprintf(stderr, "usage %s <timestamp> [program [args...]]\n", argv[0]);
		return 1;
	}

	/* Record our init time */
	snprintf(startenv, sizeof(startenv), "%ld", time(NULL));
	setenv("TIMEBOX_INIT", startenv, 1);

	/* Assume it's a timestamp */
	test = strtol(argv[1], &errptr, 0);
	if (*errptr != 0) {
		/* Is it a file? read the modification time of the file */
		struct stat sb;
		if (stat(argv[1], &sb) == 0) {
			snprintf(filebuf, sizeof(filebuf), "%ld", sb.st_mtime);
			argv[1] = filebuf;
		} else {
			fprintf(stderr, "%s: %s: invalid unix timestamp.\n", argv[0], argv[1]);
			return 2;
		}
	}

#ifdef __APPLE__
	setenv("DYLD_FORCE_FLAT_NAMESPACE", "1", 1);
	setenv("DYLD_INSERT_LIBRARIES", get_preloader(argv[0]), 1);
#else
	setenv("LD_PRELOAD", get_preloader(argv[0]), 1);
#endif
	setenv("TIMEBOX_TIME", argv[1], 1);

	if (argc < 3) {
		sub_argv = sub_shell;
		sub_argc = sizeof(sub_shell) / sizeof(sub_shell[0]);
	} else {
		sub_argv = argv + 2;
		sub_argc = argc - 2;
	}

	if ((r = execvp(sub_argv[0], sub_argv)) < 0) {
		fprintf(stderr, "%s: %s: cannot exec: %s\n", argv[0], sub_argv[0], strerror(errno));
		return 3;
	}

	return 0;
}

static const char *get_preloader(const char *progname)
{
	static char buf[256];
	ssize_t r;
	char *p;
	char *list[] = {
		".",
		"/usr/lib",
		"/usr/local/lib"
	};
	int i;

#ifdef __linux__
	if ((r = readlink("/proc/self/exe", buf, sizeof(buf) - 1 - strlen(PRELOAD_LIB))) < 0) {
		fprintf(stderr, "%s: cannot read /proc/self/exe: %s", progname, strerror(errno));
		exit(4);
	}

	/* readlink doesn't null terminate */
	buf[r] = 0;

	/* Strategy:
	 *
	 * If the path ends in /bin, we assume an install, and check the
	 * bin directory, then adjacent lib directory. If the library doesn't
	 * exist there, look in the current working directory, then finally
	 * /usr/lib and /usr/local/lib.
	 */

	p = buf + r;
	while (p >= buf && *p != '/')
		p--;
	if (p >= buf) {
		*p-- = 0;
		while (p >= buf && *p != '/')
			p--;
		if (p >= buf) {
			if (strcmp(p, "/bin") == 0) {
				strcat(p, "/" PRELOAD_LIB);
				if (access(buf, R_OK) == 0)
					return buf;
				strcpy(p, "/lib/" PRELOAD_LIB);
				if (access(buf, R_OK) == 0)
					return buf;
			}
		}
	}
#endif

	for (i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
		snprintf(buf, sizeof(buf), "%s/%s", list[i], PRELOAD_LIB);
		if (access(buf, R_OK) == 0)
			return buf;
	}

	fprintf(stderr, "%s: %s: cannot find preload library.\n", progname, PRELOAD_LIB);
	exit(5);

	return NULL;
}
