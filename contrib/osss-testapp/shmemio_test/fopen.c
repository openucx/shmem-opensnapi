// Copyright (c) 2018 - 2020 Arm, Ltd

#include <stdio.h>
#include <shmem.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

void print_fspace_stat(shmem_fspace_t fid)
{
  shmem_fspace_stat_t fstat;
  int me = shmem_my_pe ();

  if (shmem_fspace_stat(fid, &fstat) != 0) {
    printf ("%d: Failed to stat fspace\n", me);
    return;
  }

  printf ("%d: the fspace is accessible on pe (%d:%d)\n", me,
	  fstat.pe_start, fstat.pe_start + fstat.pe_size - 1);
}

void print_file_stat(shmem_fp_t *fp)
{
  int me = shmem_my_pe ();
  
  printf ("%d: Getting File stats...\n", me); fflush(stdout);
  shmem_fp_stat(fp);
  printf ("\tsize                                        = %lld\n", fp->size);
  printf ("\tctime (time loaded into current location)   = %s", ctime(&(fp->ctime)));
  printf ("\tatime (time of last file open)              = %s", ctime(&(fp->atime)));
  printf ("\tmtime (time of last file size change)       = %s", ctime(&(fp->mtime)));
  printf ("\tftime (time of last file flush)             = %s", ctime(&(fp->ftime)));
}
 

shmem_fp_t *open_testfile(shmem_fspace_t fid,
			  const char *fname, size_t fsize)
{
  int err;
  shmem_fp_t *fp = shmem_open(fid, fname,
			      fsize, -1, -1, 1, -1, &err);

  if (fp == NULL) {
    printf ("Failed to open file. Got NULL pointer. Error code is %d\n", err);
    return NULL;
  }

  printf ("shmem_fopen: fp returned is %p, addr=%lx, size=%u, unit size=%d, pe [%d:%d] by %d\n",
	  fp, (long unsigned)fp->addr, fp->size, fp->unit_size, fp->pe_start,
	  fp->pe_start + fp->pe_size - 1, fp->pe_stride);

  return fp;
}

void reset_file(shmem_fp_t *fp, size_t fsize)
{
  size_t num_ints = fsize / sizeof(int);
  int *fp_int_vec = (int*)fp->addr;
  int fpe = fp->pe_start;
  int isrc = 0;

  printf ("Resetting file %llu int contents from addr %p to %p\n",
	  num_ints, fp_int_vec, fp_int_vec + num_ints);
  
  for (size_t idx = 0; idx < num_ints; idx++) {
    shmem_int_put(fp_int_vec + idx, &isrc, 1, fpe);
  }
}

void file_open_test(shmem_fspace_t fid, const char *fname, int do_reset)
{
  int me = shmem_my_pe ();
  int npes = shmem_n_pes ();
  size_t perpe = 128;
  size_t fsize = perpe * npes;

  print_fspace_stat(fid);

  //int fname_exists = (access(fname,F_OK)==0);
  
  shmem_fp_t *fp = open_testfile(fid, fname, fsize);
  if (fp == NULL) return;

  if ((do_reset) && (me == 0)) {
    reset_file(fp, fsize);
  }

  shmem_barrier_all();
  
  char *my_mem = (char*)(fp->addr + (perpe * me));
  int pe = fp->pe_start;

  int ibuf[4];
  shmem_int_get(ibuf, (int*)my_mem, 2, pe);
  printf ("%d: My two ints in the file are: %d, %d\n", me, ibuf[0], ibuf[1]);

  ibuf[0] += me + 1;
  ibuf[1] += me + 1;

  shmem_int_put((int*)my_mem, ibuf, 2, pe);

  ibuf[0] = 0;
  ibuf[1] = 0;

  shmem_int_get(ibuf, (int*)my_mem, 2, pe);
  printf ("%d: Now my two ints in the file are: %d, %d\n", me, ibuf[0], ibuf[1]);

  shmem_barrier_all();

  int next_pe = (me + 1) % npes;
  char *next_mem = (char*)(fp->addr + (perpe * next_pe));

  shmem_int_get(ibuf, (int*)next_mem, 2, pe);
  printf ("%d: PE %d has two ints in the file: %d, %d\n", me, next_pe, ibuf[0], ibuf[1]);

  if (me == 0) {
    print_file_stat(fp);
  }
  
  shmem_close(fp, 0);
}

int main (int argc, char **argv)
{
  int me, npes;
  char hostname[1024];
  gethostname(hostname, 1024);

  if (argc != 5) {
    printf ("Usage: %s FNAME RESET HOST PORT\n");
    printf ("\tRESET=0 or 1\n");
    return 1;
  }

  const char *fname = argv[1];
  int do_reset = atoi(argv[2]);
  
  shmem_fspace_conx_t conx;
  conx.storage_server_name = argv[3];
  conx.storage_server_port = atoi(argv[4]);

  shmem_init();

  me = shmem_my_pe ();
  npes = shmem_n_pes ();

  printf ("shmem_fopen: hello from node %4d of %4d on %s\n", me, npes, hostname);
  fflush(stdout);

  shmemio_set_loglvl("warn");
  
  printf ("shmem_fopen: %d: Connect to %s:%d\n", me, conx.storage_server_name, conx.storage_server_port);
  shmem_fspace_t fid = shmem_connect(&conx);
  
  if (fid == SHMEM_NULL_FSPACE) {
    printf ("shmem_fopen: connect failed\n");
  }
  else {
    file_open_test(fid, fname, do_reset);
    shmem_disconnect(fid);
  }
  
  shmem_finalize();
}

 
