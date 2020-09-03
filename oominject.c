#include "oomify.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static struct oomctl ctl;

__attribute__((constructor))
static void init_oominject(void) {
	if (read(OOMCTL_FILENO, &ctl, sizeof(ctl)) != sizeof(ctl)) {
		abort();
	}
}

static struct oomstat stats;

__attribute__((destructor))
static void fini_oominject(void) {
	struct oomstat copy = stats;
	if (write(OOMSTAT_FILENO, &copy, sizeof(copy)) != sizeof(copy)) {
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
