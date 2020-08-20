/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef _SHMEM_API_SHMEMIO_H
#define _SHMEM_API_SHMEMIO_H 1

#include "shmem/defs_shmemio.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */


  
  shmem_fspace_t shmem_connect(shmem_fspace_conx_t *conx);

  int shmem_disconnect(shmem_fspace_t fspace);

  int shmem_fspace_stat(shmem_fspace_t fspace, shmem_fspace_stat_t *stat);


  
  shmem_fp_t *shmem_open(shmem_fspace_t fspace, const char *file, size_t fsize,
			 int pe_start, int pe_stride, int pe_size, int unit_size, int *err);
  

  int shmem_fp_stat(shmem_fp_t *fp);
  
  int shmem_fextend(shmem_fp_t *fp, size_t bytes);

  int shmem_ftrunc(shmem_fp_t *fp, size_t bytes, int ioflags);

  int shmem_close (shmem_fp_t *fp, int ioflags);

  

  int shmem_fp_flush(shmem_fp_t *fp, int ioflags);

  void shmem_fspace_flush(shmem_fspace_t fspace, int ioflags);

  void shmem_strerror(int errnum, char *strbuf);

  void shmemio_set_loglvl(char *lvl);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif
