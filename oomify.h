/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2020 Tavian Barnes <tavianator@tavianator.com>             *
 *                                                                          *
 * Permission to use, copy, modify, and/or distribute this software for any *
 * purpose with or without fee is hereby granted.                           *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES *
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         *
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  *
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   *
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    *
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  *
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           *
 ****************************************************************************/

#ifndef OOMIFY_H
#define OOMIFY_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

#define OOMCTL_FILENO 3

/**
 * OOMify configuration.
 */
struct oomctl {
	/** The allocation at which to inject a failure. */
	size_t inject_at;
	/** Whether to fail subsequent allocations as well. */
	bool inject_after;
	/** Whether to raise SIGSTOP on the first failure. */
	bool stop;
};

#define OOMSTAT_FILENO 4

/**
 * OOMify statistics.
 */
struct oomstat {
	/** The total number of allocations that occured. */
	atomic_size_t total;
	/** The number of malloc() calls. */
	atomic_size_t malloc;
	/** The number of calloc() calls. */
	atomic_size_t calloc;
	/** The number of realloc() calls. */
	atomic_size_t realloc;
	/** The number of aligned_alloc() calls. */
	atomic_size_t aligned_alloc;
	/** The number of posix_memalign() calls. */
	atomic_size_t posix_memalign;
	/** The number of memalign() calls. */
	atomic_size_t memalign;
	/** The number of free() calls. */
	atomic_size_t free;
};

#endif // OOMIFY_H
