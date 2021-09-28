#include "oomify.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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
	size_t n = __atomic_fetch_add(&stats.total, 1, __ATOMIC_SEQ_CST);

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
	__atomic_fetch_add(&stats.malloc, 1, __ATOMIC_RELAXED);

	if (should_inject()) {
		errno = ENOMEM;
		return NULL;
	} else {
		return __libc_malloc(size);
	}
}

void *calloc(size_t nmemb, size_t size) {
	__atomic_fetch_add(&stats.calloc, 1, __ATOMIC_RELAXED);

	if (should_inject()) {
		errno = ENOMEM;
		return NULL;
	} else {
		return __libc_calloc(nmemb, size);
	}
}

void *realloc(void *ptr, size_t size) {
	__atomic_fetch_add(&stats.realloc, 1, __ATOMIC_RELAXED);

	if (should_inject()) {
		errno = ENOMEM;
		return NULL;
	} else {
		return __libc_realloc(ptr, size);
	}
}

void free(void *ptr) {
	__atomic_fetch_add(&stats.free, 1, __ATOMIC_RELAXED);

	__libc_free(ptr);
}
