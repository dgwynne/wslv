/* */

/*
 * Copyright (c) 2026 David Gwynne <david@gwynne.id.au>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "wslv_profiler.h"

#define EVENT_BITS	12
#define EVENT_COUNT	(1U << EVENT_BITS)
#define EVENT_MASK	(EVENT_COUNT - 1)

struct trace_evt {
	uint64_t	 ts;
	const char	*name;
	int		 ph;
};

struct stuff {
	uint64_t	ts;
	pid_t		pid;
	pid_t		tid;
};

static struct stuff stuff;

static uint64_t
wslv_profile_ts(void)
{
	struct timespec ts;
	uint64_t rv;  

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		abort();

	rv = (uint64_t)ts.tv_sec * 1000000000ULL;
	rv += (uint64_t)ts.tv_nsec;

	return (rv);
}

void
wslv_profile_init(void)
{
	stuff.pid = getpid();
	stuff.tid = getthrid();
	stuff.ts = wslv_profile_ts();
}

void
wslv_profile_evt(int evt, const char *str)
{
#if 0
	if (str == NULL)
		return;

	fprintf(stderr, "{\"name\":\"%s\",\"ph\":\"%c\","
	    "\"ts\":%llu,\"pid\":%d,\"tid\":%d}\n",
	    str, evt,
	    wslv_profile_ts() - stuff.ts, stuff.pid, stuff.tid);
#endif
}
