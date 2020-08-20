// Copyright (c) 2018 - 2020 Arm, Ltd

#include <stdio.h>
#include <shmem.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

void access_file(shmem_fspace_t fid)
{
  char buf[SHMEM_MAX_ERRSTR];
  
  int me = shmem_my_pe ();
  int npes = shmem_n_pes ();
  size_t perpe = 128;
  size_t fsize = perpe * npes;
  
  shmem_fspace_stat_t fstat;

  int err;
  shmem_fp_t *fp = shmem_open(fid, "/tmp/shmemio_sharefile", fsize, -1, -1, 1, -1, &err);

  if (fp == NULL) {
    printf ("Failed to open file. Got NULL pointer. Error code is %d\n", err);
    return;
  }

  printf ("shmem_fopen: fp returned is %p, addr=%lx, size=%u, unit size=%d, pe [%d:%d] by %d\n",
	  fp, (long unsigned)fp->addr, fp->size, fp->unit_size, fp->pe_start,
	  fp->pe_start + fp->pe_size - 1, fp->pe_stride);
  
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
    shmem_fp_stat(fp);
    printf ("File stats:\n");
    printf ("\tsize                                        = %lld\n", fp->size);
    printf ("\tctime (time loaded into current location)   = %s", ctime(&(fp->ctime)));
    printf ("\tatime (time of last file open)              = %s", ctime(&(fp->atime)));
    printf ("\tmtime (time of last file size change)       = %s", ctime(&(fp->mtime)));
    printf ("\tftime (time of last file flush)             = %s", ctime(&(fp->ftime)));
  }

  int s = shmem_close(fp, SHMEM_IO_WAIT);
  shmem_strerror(s, buf);

  printf ("File close returned status %d: %s\n", s, buf);
}

int main (int argc, char **argv)
{
  int me, npes;
  char hostname[1024];
  gethostname(hostname, 1024);

  if (argc != 3) {
    printf ("Usage: %s HOST PORT\n");
    return 1;
  }

  shmem_fspace_conx_t conx;
  conx.storage_server_name = argv[1];
  conx.storage_server_port = atoi(argv[2]);

  shmem_init();

  me = shmem_my_pe ();
  npes = shmem_n_pes ();

  shmemio_set_loglvl("trace");
  
  printf ("shmem_fopen: %d: Connect to %s:%d\n", me, conx.storage_server_name, conx.storage_server_port);
  shmem_fspace_t fid = shmem_connect(&conx);
  
  if (fid == SHMEM_NULL_FSPACE) {
    printf ("shmem_fopen: connect failed\n");
  }
  else {
    access_file(fid);
    shmem_disconnect(fid);
  }
  
  shmem_finalize();
}

 
