#include "oomify.h"
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

static struct oomctl ctl;

__attribute__((constructor))
static void init_oominject(void) {
	if (read(OOMCTL_FILENO, &ctl, sizeof(ctl)) != sizeof(ctl)) {
		static const char message[] = "liboomify: Failed to read oomctl\n";
		write(STDERR_FILENO, message, sizeof(message) - 1);
		abort();
	}
	close(OOMCTL_FILENO);

	// Don't pass OOMSTAT_FILENO to exec'd children
	int flags = fcntl(OOMSTAT_FILENO, F_GETFD);
	if (flags >= 0 && !(flags & FD_CLOEXEC)) {
		fcntl(OOMSTAT_FILENO, F_SETFD, flags | FD_CLOEXEC);
	}

	// Don't apply oomify to any exec'd children
	unsetenv("LD_PRELOAD");
}

static struct oomstat stats;

// glibc hook to free its own private allocations
void __libc_freeres(void);

__attribute__((destructor))
static void fini_oominject(void) {
	__libc_freeres();

	struct oomstat copy = stats;
	if (write(OOMSTAT_FILENO, &copy, sizeof(copy)) != sizeof(copy)) {
		static const char message[] = "liboomify: Failed to write oomstat\n";
		write(STDERR_FILENO, message, sizeof(message) - 1);
		abort();
	}
}

static bool should_inject(atomic_size_t *stat) {
	atomic_fetch_add_explicit(stat, 1, memory_order_relaxed);

	size_t n = atomic_fetch_add_explicit(&stats.total, 1, memory_order_seq_cst);

	if (n == ctl.inject_at) {
		if (ctl.stop) {
			raise(SIGSTOP);
		}
		errno = ENOMEM;
		return true;
	} else if (ctl.inject_after && n >= ctl.inject_at) {
		errno = ENOMEM;
		return true;
	} else {
		return false;
	}
}

void *__libc_malloc(size_t size);
void *__libc_calloc(size_t nmemb, size_t size);
void *__libc_realloc(void *ptr, size_t size);
void *__libc_memalign(size_t align, size_t size);
void __libc_free(void* ptr);

void *malloc(size_t size) {
	if (should_inject(&stats.malloc)) {
		return NULL;
	}

	return __libc_malloc(size);
}

void *calloc(size_t nmemb, size_t size) {
	if (should_inject(&stats.calloc)) {
		return NULL;
	}

	return __libc_calloc(nmemb, size);
}

void *realloc(void *ptr, size_t size) {
	if (should_inject(&stats.realloc)) {
		return NULL;
	}

	return __libc_realloc(ptr, size);
}

void *aligned_alloc(size_t align, size_t size) {
	if (should_inject(&stats.aligned_alloc)) {
		return NULL;
	}

	return __libc_memalign(align, size);
}

int posix_memalign(void **memptr, size_t align, size_t size) {
	// posix_memalign() doesn't modify errno
	int saved = errno;

	if (should_inject(&stats.posix_memalign)) {
		errno = saved;
		return ENOMEM;
	}

	void *ptr = __libc_memalign(align, size);
	int error = errno;
	errno = saved;

	if (ptr) {
		*memptr = ptr;
		return 0;
	} else {
		return error;
	}
}

void *memalign(size_t align, size_t size) {
	if (should_inject(&stats.memalign)) {
		return NULL;
	}

	return __libc_memalign(align, size);
}

void free(void *ptr) {
	atomic_fetch_add_explicit(&stats.free, 1, memory_order_relaxed);

	__libc_free(ptr);
}
