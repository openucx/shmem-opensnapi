// Copyright (c) 2018 - 2020 Arm, Ltd

#define _GNU_SOURCE

#include <shmem.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "timer.h"

#define GET_INT_SHMEM_FP   0
#define FLUSH_SHMEM_FP     1
#define PUT_INT_SHMEM_FP   2
#define GET_INT_FILE       3
#define FLUSH_FILE         4
#define PUT_INT_FILE       5
#define N_TIMERS           6

const size_t fsize = 1L<<28;
const int num_iters = 1;

#define nints (1<<20)
static int ibuf[nints];

my_timer_t timers[N_TIMERS];

void make_file(char *fname)
{
  size_t tot_ints = fsize / sizeof(int);
  
  int fd = open(fname, O_CREAT | O_RDWR, S_IRWXU);

  if (fd < 0) {
    perror("failed to open file");
    return;
  }

  printf("Creating %llu bytes of data into fd %d\n", nints * sizeof(int), fd);

#pragma omp parallel for
  for (size_t idx = 0; idx < nints; idx++) {
    ibuf[idx] = rand();
  }

  for (size_t idx = 0; idx < tot_ints; idx += nints) {
    write(fd, ibuf, nints * sizeof(int));
  }
  
  close(fd);
}


void rw_file(char *fname)
{
  int me = shmem_my_pe ();
  int npes = shmem_n_pes ();

  int fd = open(fname, O_SYNC | O_DIRECT | O_RDWR);
  shmem_barrier_all();

  size_t perpe = fsize / npes;
  size_t int_perpe = perpe / sizeof(int);
  size_t my_offset = perpe * me;

  size_t *roffs;
  size_t iters;
  
  printf ("%d: Access %d ints at address [%lx:%lx] (size=%u)\n",
	  me, int_perpe, (long unsigned)my_offset, (long unsigned)my_offset + perpe, perpe);

  timer_start(&(timers[GET_INT_FILE]));
  lseek(fd, my_offset, SEEK_SET);
  for (size_t idx = 0; idx < int_perpe; idx += nints) {
    const size_t nread = (idx + nints) > int_perpe ? int_perpe - idx : nints;
    read(fd, ibuf, nread * sizeof(int));
  }
  timer_stop(&(timers[GET_INT_FILE]));

  printf ("%d: My two ints in the file are: %d, %d\n", me, ibuf[0], ibuf[1]);
  
  ibuf[0] += me + 1;
  ibuf[1] += me + 1;

  timer_start(&(timers[PUT_INT_FILE]));
  lseek(fd, my_offset, SEEK_SET);
  for (size_t idx = 0; idx < int_perpe; idx += nints) {
    const size_t nwrite = (idx + nints) > int_perpe ? int_perpe - idx : nints;
    write(fd, ibuf, nwrite * sizeof(int));
  }
  timer_stop(&(timers[PUT_INT_FILE]));

  timer_start(&(timers[FLUSH_FILE]));
  fsync(fd);
  timer_stop(&(timers[FLUSH_FILE]));

  shmem_barrier_all();

  close(fd);
  
}


void rw_fspace(shmem_fspace_t fid, char *fname)
{
  int me = shmem_my_pe ();
  int npes = shmem_n_pes ();

  shmem_fspace_stat_t fstat;
  
  if (shmem_fspace_stat(fid, &fstat) != 0) {
    printf ("Failed to stat fspace\n");
    return;
  }

  printf ("shmem_fopen: the fspace is accessible on pe (%d:%d)\n",
	  fstat.pe_start, fstat.pe_start + fstat.pe_size - 1);

  //int nfpes = npes;
  //while (nfpes > fstat.pe_size) {
  //  nfpes >>= 1;
  //}

  int err;
  shmem_fp_t *fp = shmem_open(fid, fname, fsize, -1, -1, 1, -1, &err);

  if (fp == NULL) {
    printf ("Failed to open file. Got NULL pointer. Error code is %d\n", err);
    return;
  }

  printf ("shmem_fopen: fp returned is %p, addr=%lx, size=%u, unit size=%d, pe [%d:%d] by %d\n",
	  fp, (long unsigned)fp->addr, fp->size, fp->unit_size, fp->pe_start,
	  fp->pe_start + fp->pe_size - 1, fp->pe_stride);

  if (fp->size < fsize) {
    printf ("File is incorrect size. %llu < %llu\n", fp->size, fsize);
    shmem_close(fp, 0);
    return;
  }

  int pes_per_fpe = npes / fp->pe_size;
  int my_fpe = fp->pe_start + (me / pes_per_fpe);

  printf ("%d: There are %d pes per fpe. My fpe is %d\n", me, pes_per_fpe, my_fpe);

  size_t perpe = fsize / npes;
  size_t int_perpe = perpe / sizeof(int);

  char *my_mem = (char*)(fp->addr + (perpe * (me % pes_per_fpe)));

  printf ("%d: Access %d ints into ibuf %p at address [%lx:%lx] (size=%u) on fpe %d\n",
	  me, int_perpe, ibuf, (long unsigned)my_mem, (long unsigned)my_mem + perpe, perpe, my_fpe);

  timer_start(&(timers[GET_INT_SHMEM_FP]));

  for (size_t idx = 0; idx < int_perpe; idx += nints) {
    const size_t nget = (idx + nints) > int_perpe ? int_perpe - idx : nints;
    shmem_int_get(ibuf, (int*)my_mem + idx, nget, my_fpe);
  }

  timer_stop(&(timers[GET_INT_SHMEM_FP]));

  printf ("%d: My two ints in the file are: %d, %d\n", me, ibuf[0], ibuf[1]);
  
  ibuf[0] += me + 1;
  ibuf[1] += me + 1;

  timer_start(&(timers[PUT_INT_SHMEM_FP]));

  for (size_t idx = 0; idx < int_perpe; idx += nints) {
    const size_t nput = (idx + nints) > int_perpe ? int_perpe - idx : nints;
    shmem_int_put((int*)my_mem + idx, ibuf, nput, my_fpe);
  }
  
  timer_stop(&(timers[PUT_INT_SHMEM_FP]));

  
  timer_start(&(timers[FLUSH_SHMEM_FP]));
  if (me == 0) {
    shmem_fp_flush(fp, 0);
  }
  timer_stop(&(timers[FLUSH_SHMEM_FP]));

  shmem_barrier_all();

  shmem_close(fp, 0);
}

int main (int argc, char **argv)
{
  int me, npes;
  char hostname[1024];
  gethostname(hostname, 1024);

  if ((argc != 2) && (argc != 4)) {
    printf ("Usage: %s FNAME [HOST PORT]\n", argv[0]);
    printf ("argc = %d\n", argc);
    return 1;
  }
  
  char *fname = argv[1];
  if (argc == 2) {
    make_file(fname);
    return 0;
  }
  
  shmem_fspace_conx_t conx;
  conx.storage_server_name = argv[2];
  conx.storage_server_port = atoi(argv[3]);


  shmem_init();

  me = shmem_my_pe ();
  npes = shmem_n_pes ();

  printf ("shmem_fopen: hello from node %4d of %4d on %s\n", me, npes, hostname);
  fflush(stdout);

  shmemio_set_loglvl("info");
  
  printf ("shmem_fopen: %d: Connect to %s:%d\n", me, conx.storage_server_name, conx.storage_server_port);
  shmem_fspace_t fid = shmem_connect(&conx);
  
  if (fid == SHMEM_NULL_FSPACE) {
    printf ("shmem_fopen: connect failed\n");
  }
  else {
    for (int idx = 0; idx < N_TIMERS; idx++) {
      timer_reset(timers + idx);
    }
    for (int idx = 0; idx < num_iters; idx++) {
      rw_fspace(fid, fname);
      rw_file(fname);
    }
    shmem_disconnect(fid);
  }
  
  shmem_barrier_all();

  print_serial(&(timers[GET_INT_FILE]), "get file", me, npes);
  print_serial(&(timers[PUT_INT_FILE]), "put file", me, npes);
  print_serial(&(timers[FLUSH_FILE]), "flush file", me, npes);

  print_serial(&(timers[GET_INT_SHMEM_FP]), "get fspace", me, npes);
  print_serial(&(timers[PUT_INT_SHMEM_FP]), "put fspace", me, npes);
  print_serial(&(timers[FLUSH_SHMEM_FP]), "flush fp", me, npes);

  shmem_finalize();
}

 
