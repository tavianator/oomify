OOMify
======

A utility for injecting memory allocation failures.

Error handling is notoriously hard to test, especially for C code.
OOMify helps by causing `malloc()` and related functions to fail in a controlled way, with the help of `LD_PRELOAD`.
In its default mode of operation, OOMify

1. Runs the program once to count the number of allocations it performs:

       $ oomify bfs >/dev/null
       oomify: bfs did 2311 allocations
       ...

2. Runs the program that many times, injecting a failure at each separate allocation:

       ...
       malloc(): Cannot allocate memory
       malloc(): Cannot allocate memory
       cfdup(): Cannot allocate memory
       cfdup(): Cannot allocate memory
       cannot allocate memory for thread-local data: ABORT
       Assertion 's' failed at src/shared/json.c:1760, function json_variant_format(). Aborting.
       oomify: alloc 1285: bfs terminated with signal 6 (Aborted)
       Assertion 's' failed at src/shared/json.c:1760, function json_variant_format(). Aborting.
       oomify: alloc 1314: bfs terminated with signal 6 (Aborted)
       munmap_chunk(): invalid pointer
       oomify: alloc 1486: bfs terminated with signal 6 (Aborted)
       munmap_chunk(): invalid pointer
       oomify: alloc 1490: bfs terminated with signal 6 (Aborted)
       oomify: alloc 1494: bfs terminated with signal 11 (Segmentation fault)
       ...

If you uncover a bug, OOMify can zero in on it to help you debug it.
Let's look at allocation 1486, which corresponds with the `munmap_chunk(): invalid pointer` error.
We'll pass `-n1486` to fail that specific allocation, and `-s` to automatically raise `SIGSTOP` so we can attach a debugger before the crash:

    $ oomify -n1486 -s bfs >/dev/null

Then from another terminal:

    $ gdb bfs $(pgrep bfs)
    GNU gdb (GDB) 9.2
    ...
    0x00007fdac07325f3 in raise () from /usr/lib/libc.so.6
    (gdb) bt
    #0  0x00007fdac07325f3 in raise () from /usr/lib/libc.so.6
    #1  0x00007fdac08d4283 in should_inject () at oominject.c:31
    #2  0x00007fdac08d42ca in malloc (size=5) at oominject.c:49
    #3  0x00007fdac0784b4f in strdup () from /usr/lib/libc.so.6
    #4  0x000055f3e91db121 in bfs_parse_groups () at pwcache.c:187
    #5  0x000055f3e91d8620 in parse_cmdline (argc=1, argv=0x7ffd9f01d958) at parse.c:3555
    #6  0x000055f3e91cdfdb in main (argc=1, argv=0x7ffd9f01d958) at main.c:103
    (gdb) frame 4
    #4  0x000055f3e91db121 in bfs_parse_groups () at pwcache.c:187
    187                     ent->gr_name = strdup(ent->gr_name);
    (gdb) cont
    Continuing.

    Program received signal SIGABRT, Aborted.
    0x00007fdac0732615 in raise () from /usr/lib/libc.so.6
    (gdb) bt
    #0  0x00007fdac0732615 in raise () from /usr/lib/libc.so.6
    #1  0x00007fdac071b862 in abort () from /usr/lib/libc.so.6
    #2  0x00007fdac07745e8 in __libc_message () from /usr/lib/libc.so.6
    #3  0x00007fdac077c27a in malloc_printerr () from /usr/lib/libc.so.6
    #4  0x00007fdac077c6ac in munmap_chunk () from /usr/lib/libc.so.6
    #5  0x00007fdac08d43a1 in free (ptr=0x55f3ea69f8b9) at oominject.c:82
    #6  0x000055f3e91db42b in bfs_free_groups (groups=0x55f3ea69f850) at pwcache.c:271
    #7  0x000055f3e91db2fb in bfs_parse_groups () at pwcache.c:240
    #8  0x000055f3e91d8620 in parse_cmdline (argc=1, argv=0x7ffd9f01d958) at parse.c:3555
    #9  0x000055f3e91cdfdb in main (argc=1, argv=0x7ffd9f01d958) at main.c:103
    (gdb) frame 6
    #6  0x000055f3e91db42b in bfs_free_groups (groups=0x55f3ea69f850) at pwcache.c:271
    271                                     free(entry->gr_mem[j]);

Here we've seen that if the `strdup()` on line 187 fails, we end up freeing an invalid pointer on line 271.
This happens because that particular error path skips over the initialization of `ent->gr_mem`.
We can fix the bug by moving the initialization earlier:

    $ git diff
    diff --git a/pwcache.c b/pwcache.c
    index 4fca134..f77f702 100644
    --- a/pwcache.c
    +++ b/pwcache.c
    @@ -184,14 +184,16 @@ struct bfs_groups *bfs_parse_groups(void) {
                    }

                    ent = groups->entries + darray_length(groups->entries) - 1;
    +
    +               char **members = ent->gr_mem;
    +               ent->gr_mem = NULL;
    +
                    ent->gr_name = strdup(ent->gr_name);
                    if (!ent->gr_name) {
                            error = errno;
                            goto fail_end;
                    }

    -               char **members = ent->gr_mem;
    -               ent->gr_mem = NULL;
                    for (char **mem = members; *mem; ++mem) {
                            char *dup = strdup(*mem);
                            if (!dup) {

OOMify confirms that the fix works:

    $ oomify -n1486 bfs >/dev/null
    oomify: alloc 1486: bfs exited with status 0
