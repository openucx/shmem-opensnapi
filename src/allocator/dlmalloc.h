/* For license: see LICENSE file at top-level */

/*
 * This was taken from the dlmalloc.c to act as a standalone header
 * file for other parts of the OpenSHMEM library
 *
 * License as for dlmalloc.c
 */

#ifndef _DLMALLOC_H
#define _DLMALLOC_H 1

#include <sys/types.h>
#include <time.h>

typedef void *mspace;

extern mspace create_mspace_with_base(void *base, size_t capacity, int locked);
extern size_t destroy_mspace(mspace msp);
extern void   *mspace_malloc(mspace msp, size_t bytes);
extern void   *mspace_calloc(mspace msp, size_t count, size_t bytes);
extern void   *mspace_realloc(mspace msp, void *mem, size_t newsize);
extern void   *mspace_realloc_in_place(mspace msp, void *mem, size_t newsize);
extern void   *mspace_memalign(mspace msp, size_t alignment, size_t bytes);
extern void   mspace_free(mspace msp, void *mem);
extern size_t mspace_footprint(mspace msp);

#ifndef STRUCT_MALLINFO_DECLARED
#define MALLINFO_FIELD_TYPE size_t
/* HP-UX (and others?) redefines mallinfo unless _STRUCT_MALLINFO is defined */
#define _STRUCT_MALLINFO
#define STRUCT_MALLINFO_DECLARED 1
struct mallinfo {
  MALLINFO_FIELD_TYPE arena;    /* non-mmapped space allocated from system */
  MALLINFO_FIELD_TYPE ordblks;  /* number of free chunks */
  MALLINFO_FIELD_TYPE smblks;   /* always 0 */
  MALLINFO_FIELD_TYPE hblks;    /* always 0 */
  MALLINFO_FIELD_TYPE hblkhd;   /* space in mmapped regions */
  MALLINFO_FIELD_TYPE usmblks;  /* maximum total allocated space */
  MALLINFO_FIELD_TYPE fsmblks;  /* always 0 */
  MALLINFO_FIELD_TYPE uordblks; /* total allocated space */
  MALLINFO_FIELD_TYPE fordblks; /* total free space */
  MALLINFO_FIELD_TYPE keepcost; /* releasable (via malloc_trim) space */
};
typedef struct mallinfo mallinfo_t;
#endif /* STRUCT_MALLINFO_DECLARED */

extern struct mallinfo mspace_mallinfo(mspace msp);

#endif /* ! _DLMALLOC_H */
