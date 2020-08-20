/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

//Easier on the build system than trying to setup VPATH, fix deps, etc
//Using this for the standalone server so we don't have to compile it against other osss deps

#define HAVE_MMAP 0
#define ONLY_MSPACES 1
#define HAVE_MORECORE 0
#define HAVE_MMAP 0
#define USE_LOCKS 1
#define REALLOC_ZERO_BYTES_FREES 1

#include "allocator/dlmalloc.h"
#include "allocator/dlmalloc.c"
