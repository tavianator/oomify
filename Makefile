############################################################################
# oomify                                                                   #
# Copyright (C) 2020 Tavian Barnes <tavianator@tavianator.com>             #
#                                                                          #
# Permission to use, copy, modify, and/or distribute this software for any #
# purpose with or without fee is hereby granted.                           #
#                                                                          #
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES #
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         #
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  #
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   #
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    #
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  #
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           #
############################################################################

CC ?= gcc
INSTALL ?= install
MKDIR ?= mkdir -p
RM ?= rm -f

CFLAGS ?= -g -Wall -Wmissing-declarations -Wstrict-prototypes
LDFLAGS ?=
DEPFLAGS ?= -MD -MP -MF $(@:.o=.d)

DESTDIR ?=
PREFIX ?= /usr

LOCAL_CPPFLAGS := -D_DEFAULT_SOURCE
LOCAL_CFLAGS := -std=c99 -fPIC

ALL_CPPFLAGS = $(LOCAL_CPPFLAGS) $(CPPFLAGS)
ALL_CFLAGS = $(ALL_CPPFLAGS) $(LOCAL_CFLAGS) $(CFLAGS) $(DEPFLAGS)
ALL_LDFLAGS = $(ALL_CFLAGS) $(LOCAL_LDFLAGS) $(LDFLAGS)

default: oomify liboomify.so

oomify: oomify.o
	$(CC) $(ALL_LDFLAGS) -pie $^ -o $@

liboomify.so: oominject.o
	$(CC) $(ALL_LDFLAGS) -shared $^ -o $@

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

clean:
	$(RM) oomify liboomify.so *.o

install:
	$(MKDIR) $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m755 oomify $(DESTDIR)$(PREFIX)/bin/oomify
	$(MKDIR) $(DESTDIR)$(MANDIR)/lib
	$(INSTALL) -m755 liboomify.so $(DESTDIR)$(MANDIR)/lib/liboomify.so

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/bin/bfs
	$(RM) $(DESTDIR)$(MANDIR)/man1/bfs.1

.PHONY: clean install uninstall
