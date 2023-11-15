#include "oomify.h"
#include <errno.h>
#include <fcntl.h>
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

__attribute__((destructor))
static void fini_oominject(void) {
	struct oomstat copy = stats;
	if (write(OOMSTAT_FILENO, &copy, sizeof(copy)) != sizeof(copy)) {
		static const char message[] = "liboomify: Failed to write oomstat\n";
		write(STDERR_FILENO, message, sizeof(message) - 1);
		abort();
	}
}

static bool should_inject(void) {
	size_t n = atomic_fetch_add_explicit(&stats.total, 1, memory_order_seq_cst);

	if (n == ctl.inject_at) {
		if (ctl.stop) {
			raise(SIGSTOP);
		}
		return true;
	} else if (ctl.inject_after && n >= ctl.inject_at) {
		return true;
	} else {
		return false;
	}
}

void *__libc_malloc(size_t size);
void *__libc_calloc(size_t nmemb, size_t size);
void *__libc_realloc(void *ptr, size_t size);
void __libc_free(void* ptr);

void *malloc(size_t size) {
	atomic_fetch_add_explicit(&stats.malloc, 1, memory_order_relaxed);

	if (should_inject()) {
		errno = ENOMEM;
		return NULL;
	} else {
		return __libc_malloc(size);
	}
}

void *calloc(size_t nmemb, size_t size) {
	atomic_fetch_add_explicit(&stats.calloc, 1, memory_order_relaxed);

	if (should_inject()) {
		errno = ENOMEM;
		return NULL;
	} else {
		return __libc_calloc(nmemb, size);
	}
}

void *realloc(void *ptr, size_t size) {
	atomic_fetch_add_explicit(&stats.realloc, 1, memory_order_relaxed);

	if (should_inject()) {
		errno = ENOMEM;
		return NULL;
	} else {
		return __libc_realloc(ptr, size);
	}
}

void free(void *ptr) {
	atomic_fetch_add_explicit(&stats.free, 1, memory_order_relaxed);

	__libc_free(ptr);
}
