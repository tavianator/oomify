/****************************************************************************
 * oomify                                                                   *
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

#include "oomify.h"
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int oomify_spawn(char *argv[], const struct oomctl *ctl, struct oomstat *stats, int *wstatus) {
	int ret = -1, error = 0;

	int ctlpipe[2], statpipe[2];

	if (pipe(ctlpipe) != 0) {
		error = errno;
		goto out;
	}
	if (pipe(statpipe) != 0) {
		error = errno;
		goto out_ctlpipe;
	}

        posix_spawn_file_actions_t actions;
        if (posix_spawn_file_actions_init(&actions) != 0) {
		goto out_statpipe;
	}

	// Set up the oomctl pipe
	if (posix_spawn_file_actions_addclose(&actions, ctlpipe[1]) != 0) {
		error = errno;
		goto out_actions;
	}
	if (ctlpipe[0] != OOMCTL_FILENO) {
		if (posix_spawn_file_actions_adddup2(&actions, ctlpipe[0], OOMCTL_FILENO) != 0) {
			error = errno;
			goto out_actions;
		}
		if (posix_spawn_file_actions_addclose(&actions, ctlpipe[0]) != 0) {
			error = errno;
			goto out_actions;
		}
	}

	// Set up the oomstat pipe
	if (posix_spawn_file_actions_addclose(&actions, statpipe[0]) != 0) {
		goto out_actions;
	}
	if (statpipe[1] != OOMSTAT_FILENO) {
		if (posix_spawn_file_actions_adddup2(&actions, statpipe[1], OOMSTAT_FILENO) != 0) {
			error = errno;
			goto out_actions;
		}
		if (posix_spawn_file_actions_addclose(&actions, statpipe[1]) != 0) {
			error = errno;
			goto out_actions;
		}
	}

	// Set LD_PRELOAD
	extern char **environ;
	size_t envc;
	for (envc = 0; environ[envc]; ++envc);
	++envc;

	char **envp = malloc((envc + 1)*sizeof(*envp));
	if (!envp) {
		goto out_actions;
	}

	memcpy(envp, environ, (envc - 1)*sizeof(*envp));
	envp[envc - 1] = "LD_PRELOAD=liboomify.so";
	envp[envc] = NULL;

	pid_t pid;
	error = posix_spawnp(&pid, argv[0], &actions, NULL, argv, envp);
	if (error != 0) {
		goto out_envp;
	}

	ret = 0;

	close(ctlpipe[0]);
	ctlpipe[0] = -1;

	errno = 0;
	if (write(ctlpipe[1], ctl, sizeof(*ctl)) != sizeof(*ctl)) {
		ret = -1;
		error = errno;
		if (error == 0) {
			error = EIO;
		}
	}

	close(ctlpipe[1]);
	ctlpipe[1] = -1;

	close(statpipe[1]);
	statpipe[1] = -1;

	errno = 0;
	if (stats && read(statpipe[0], stats, sizeof(*stats)) != sizeof(*stats)) {
		ret = -1;
		error = errno;
		if (error == 0) {
			error = EIO;
		}
	}

	if (waitpid(pid, wstatus, 0) < 0) {
		ret = -1;
		error = errno;
	}

out_envp:
	free(envp);
out_actions:
	posix_spawn_file_actions_destroy(&actions);
out_statpipe:
	if (statpipe[1] >= 0) {
		close(statpipe[1]);
	}
	if (statpipe[0] >= 0) {
		close(statpipe[0]);
	}
out_ctlpipe:
	if (ctlpipe[1] >= 0) {
		close(ctlpipe[1]);
	}
	if (ctlpipe[0] >= 0) {
		close(ctlpipe[0]);
	}
out:
	errno = error;
	return ret;
}

static int parse_int(const char *str, size_t *n) {
	char *end;
	unsigned long value = strtoul(str, &end, 10);
	if (*str != '\0' && *end == '\0') {
		*n = value;
		return 0;
	} else {
		return -1;
	}
}

static void usage(void) {
	fprintf(stderr, "Usage: oomify [-a|-n N|-d] [-f] [-s] [-v|-q] [--] PROGRAM [ARGS...]\n\n");

        fprintf(stderr, "      -a\n");
        fprintf(stderr, "          Try failing all allocations the program performs (default)\n");
        fprintf(stderr, "      -n N\n");
        fprintf(stderr, "          Run the program once, failing the Nth allocation\n");
        fprintf(stderr, "      -d\n");
        fprintf(stderr, "          Do a dry run, without failing any allocations\n");
        fprintf(stderr, "      -f\n");
        fprintf(stderr, "          Fail all subsequent allocation after the first one fails\n");
        fprintf(stderr, "      -s\n");
        fprintf(stderr, "          Raise SIGSTOP on failing allocations, to allow attaching a debugger\n");
        fprintf(stderr, "      -v\n");
        fprintf(stderr, "          Be verbose\n");
        fprintf(stderr, "      -q\n");
        fprintf(stderr, "          Be quiet\n");
}

int main(int argc, char *argv[]) {
	struct oomctl ctl = {
		.inject_at = SIZE_MAX,
	};
	bool dry_run = false;
	bool verbose = false;
	bool quiet = false;

	int opt;
	while ((opt = getopt(argc, argv, "+:an:fdsvq")) != -1) {
		switch (opt) {
		case 'a':
			ctl.inject_at = SIZE_MAX;
			break;
		case 'n':
			if (parse_int(optarg, &ctl.inject_at) != 0) {
				usage();
				return EXIT_FAILURE;
			}
			break;
		case 'f':
			ctl.inject_after = true;
			break;
		case 'd':
			dry_run = true;
			break;
		case 's':
			ctl.stop = true;
			break;
		case 'v':
			verbose = true;
			quiet = false;
			break;
		case 'q':
			verbose = false;
			quiet = true;
			break;
		default:
			usage();
			return EXIT_FAILURE;
		}
	}

	if (optind >= argc) {
		usage();
		return EXIT_FAILURE;
	}
	char **spawn_argv = argv + optind;

	if (dry_run) {
		ctl.inject_at = SIZE_MAX;
	}

	size_t min, max;
	if (ctl.inject_at == SIZE_MAX) {
		struct oomstat stats;
		if (oomify_spawn(spawn_argv, &ctl, &stats, NULL) != 0) {
			perror("oomify_spawn()");
			return EXIT_FAILURE;
		}

		size_t total = stats.total;
		size_t blocks = stats.malloc + stats.calloc + stats.realloc_null
			+ stats.aligned_alloc + stats.posix_memalign + stats.memalign;
		size_t leaks = blocks - stats.free_ptr;

		min = 0;
		max = dry_run ? 0 : total;

		if (!quiet) {
			fprintf(stderr, "oomify --");
			for (size_t i = 0; spawn_argv[i]; ++i) {
				fprintf(stderr, " %s", spawn_argv[i]);
			}
			fprintf(stderr, "\n");

			if (verbose) {
				fprintf(stderr, "    ------------------------------\n");
				fprintf(stderr, "        malloc():         %8zu\n", stats.malloc);
				fprintf(stderr, "      + calloc():         %8zu\n", stats.calloc);
				fprintf(stderr, "      + realloc(NULL):    %8zu\n", stats.realloc_null);
				fprintf(stderr, "      + aligned_alloc():  %8zu\n", stats.aligned_alloc);
				fprintf(stderr, "      + posix_memalign(): %8zu\n", stats.posix_memalign);
				fprintf(stderr, "      + memalign():       %8zu\n", stats.memalign);
				fprintf(stderr, "    ------------------------------\n");
			}
			fprintf(stderr, "    allocated blocks:     %8zu\n", blocks);

			if (verbose) {
				fprintf(stderr, "    ------------------------------\n");
				fprintf(stderr, "      + realloc(ptr):     %8zu\n", stats.realloc_ptr);
				fprintf(stderr, "    ------------------------------\n");
			}
			fprintf(stderr, "    fallible allocations: %8zu\n", total);

			if (verbose) {
				fprintf(stderr, "    ------------------------------\n");
				fprintf(stderr, "      - free(ptr):        %8zu // free(NULL): %zu\n", stats.free_ptr, stats.free_null);
				fprintf(stderr, "    ------------------------------\n");
 			}
			fprintf(stderr, "    leaks:                %8zu\n", leaks);
		}
	} else {
		min = ctl.inject_at;
		max = min + 1;
	}

	if (!quiet && max == min + 1) {
		verbose = true;
	}

	for (size_t i = min; i < max; ++i) {
		ctl.inject_at = i;

		int wstatus;
		if (oomify_spawn(spawn_argv, &ctl, NULL, &wstatus) != 0) {
			perror("oomify_spawn()");
			return EXIT_FAILURE;
		}

		if (!quiet && WIFSIGNALED(wstatus)) {
			int sig = WTERMSIG(wstatus);
			fprintf(stderr, "oomify: alloc %zu: %s terminated with signal %d (%s)\n", i, spawn_argv[0], sig, strsignal(sig));
		} else if (verbose && WIFEXITED(wstatus)) {
			fprintf(stderr, "oomify: alloc %zu: %s exited with status %d\n", i, spawn_argv[0], WEXITSTATUS(wstatus));
		}
	}

	return EXIT_SUCCESS;
}
