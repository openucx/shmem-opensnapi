// Copyright (c) 2018 - 2020 Arm, Ltd

#include <stdio.h>
#include <shmem.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

void do_stat(shmem_fspace_t fid)
{
  shmem_fspace_stat_t fstat;
  int ret = shmem_fspace_stat(fid, &fstat);

  if (ret != 0) {
    char errstr[SHMEM_MAX_ERRSTR];
    shmem_strerror(ret, errstr);
    printf ("Failed to stat fspace: %s\n", errstr);
    return;
  }

  printf ("Connected to file space ID %d\n", fid);
  printf ("\tpe start        = %d\n", fstat.pe_start);
  printf ("\tpe size         = %d\n", fstat.pe_size);
  printf ("\t# regions       = %d\n", fstat.nregions);
  printf ("\t# loaded files  = %d\n", fstat.nfiles);
  printf ("\tused size       = %d\n", fstat.used_size);
  printf ("\tfree size       = %d\n", fstat.free_size);
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

  printf ("shmem_connect: hello from node %4d of %4d on %s\n", me, npes, hostname);
  fflush(stdout);

  shmemio_set_loglvl("info");
  
  if (me == 0) {
    printf ("shmem_connect: %d: Connect to %s:%d\n", me, conx.storage_server_name, conx.storage_server_port);
    shmem_fspace_t fid = shmem_connect(&conx);

    if (fid == SHMEM_NULL_FSPACE) {
      printf ("shmem_connect: failed\n");
    }
    else {
      printf ("shmem_connect: success!\n");
      do_stat(fid);
      shmem_disconnect(fid);
    }
  }
  
  shmem_finalize();
}

 
