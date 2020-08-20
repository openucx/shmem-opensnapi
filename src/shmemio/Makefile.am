# For license: see LICENSE file at top-level
# Copyright (c) 2018 - 2020 Arm, Ltd 

### Makefile for shmemio libraries and server components

OTHER_CPPFLAGS            = -I$(srcdir)/../shmemu -I$(srcdir)/../shmemc\
			    -I../../include -I$(srcdir)/../../include -I$(srcdir)/..

MY_SERVER_SOURCES         = server_init.c server_connect.c \
                            server_fopen.c server_pmem.c

LIBSHMEMIO_SOURCES         = client_connect.c client_fspace.c \
                             $(MY_SERVER_SOURCES)

# Allow standalone server build without all osss deps
LIBSHMEMIO_SERVER_SOURCES = dlmalloc_includer.c $(MY_SERVER_SOURCES)

lib_LTLIBRARIES           = libshmemio.la libshmemio-server.la

libshmemio_la_SOURCES  = $(LIBSHMEMIO_SOURCES)
libshmemio_la_CFLAGS   = $(UCX_CFLAGS) $(EXTRA_CFLAGS) $(OTHER_CPPFLAGS)

libshmemio_server_la_SOURCES  = $(LIBSHMEMIO_SERVER_SOURCES)
libshmemio_server_la_CFLAGS   = $(UCX_CFLAGS) $(EXTRA_CFLAGS) $(OTHER_CPPFLAGS) $(DLMALLOC_CFLAGS)

lib_LIBRARIES             = libshmemio.a libshmemio-server.a

libshmemio_a_SOURCES   = $(LIBSHMEMIO_SOURCES)
libshmemio_a_CFLAGS    = $(UCX_CFLAGS) $(EXTRA_CFLAGS) $(OTHER_CPPFLAGS)

libshmemio_server_a_SOURCES   = $(LIBSHMEMIO_SERVER_SOURCES)
libshmemio_server_a_CFLAGS    = $(UCX_CFLAGS) $(EXTRA_CFLAGS) $(OTHER_CPPFLAGS)

# Standalone test server
bin_PROGRAMS = fspace_server

fspace_server_SOURCES  = fspace_server.c
fspace_server_CPPFLAGS = -I$(srcdir) -I$(srcdir)/..
fspace_server_LDFLAGS  = -L.
fspace_server_LDADD    = libshmemio-server.a
