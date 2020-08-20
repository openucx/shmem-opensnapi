/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef _SHMEM_DEFINES_SHMEMIO_H
#define _SHMEM_DEFINES_SHMEMIO_H 1

  /* I/O Function API */
#define SHMEM_NULL_FSPACE -1
#define SHMEM_MAX_ERRSTR 256

// flags for flush io
#define SHMEM_IO_DEEP_FLUSH        0x1
#define SHMEM_IO_POP_FLUSH         0x2
#define SHMEM_IO_DEALLOC           0x4
#define SHMEM_IO_WAIT              0x8
#define SHMEM_IO_RELOC             0x10

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

  typedef int shmem_fspace_t;
  
  typedef struct shmem_fspace_conx_s {
    char    *storage_server_name;
    unsigned storage_server_port;
    int      flags;
  } shmem_fspace_conx_t;

  typedef struct shmem_fp_s {
    /* do not move fields around in this struct */
    void *addr;
    size_t size;
    int unit_size;
    int pe_start;
    int pe_stride;
    int pe_size;

    uint64_t fkey;

    time_t ctime; //time the file was loaded into current location
    time_t atime; //time of last file open
    time_t mtime; //time of last ftrunc or fextend
    time_t ftime; //time of last flush
  } shmem_fp_t;

  typedef struct shmem_fspace_stat_s {
    int pe_start;
    int pe_size;

    int nregions;
    int nfiles;
    
    size_t used_size;
    size_t free_size;
  } shmem_fspace_stat_t;
  
#ifdef __cplusplus
}
#endif  /* __cplusplus */


#endif
