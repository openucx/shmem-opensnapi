/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif /* HAVE_CONFIG_H */

#include "shmemu.h"
#include "shmemc.h"
#include "shmem.h"

#include "shmemio.h"
#include "shmemio_client.h"

#include "shmemio_test_util.h"
#include "shmemio_client_fpe.h"
#include "shmemio_client_region.h"
#include "shmemio_stream_util.h"

/*
 * How many fspace objects to malloc at a time when none are available
 * Only need one fspace object per shmem_connect call, even if connecting to large number of fpes
 * So this number should be ok to be small
 */
#define SHMEMIO_FSPACE_NALLOC 8

/* 
 * Initialize a newly malloced set of shmemio_fspace_t objects
 */
static inline void
fspace_reset(int fid)
{
  shmemio_fspace_t *fio = &(proc.io.fspaces[fid]);
  
  fio->ch = (shmemc_context_h) SHMEM_CTX_DEFAULT;
  fio->valid = 0;
  
  fio->req_ep = NULL;
  
  fio->nfpes = 0;
  fio->fpe_start = -1;
  fio->fpe_end = -1;
  
  fio->nregions = 0;
  fio->l_regions = NULL;

  fio->used_addrs = NULL;
}


/*
 * Release all the resources used by an fpsace and its fpes
 */
int
shmemio_release_fspace(int fid)
{
  shmemio_fspace_t* fio = &proc.io.fspaces[fid];

  fspace_release_client_regions(fio);

  fspace_release_fpes(fio);

  if (fio->req_ep != NULL) { shmemio_ep_force_close(fio->ch->w, fio->req_ep); }

  if (fio->used_addrs != NULL) {
    free(fio->used_addrs);
  }
  
  fspace_reset(fid);
}

/* 
 * Find an unused fspace object or malloc some new ones if there are none unused
 * This is not real efficient because we don't expect to connect to many fspaces.
 * Maybe 1 or 2 in the whole program.
 * So for now, having and maintaining a free list does not seem worth the added code complexity
 */
int
shmemio_get_new_fspace()
{
  if (proc.io.fspaces == NULL) {
    proc.io.fspaces = (shmemio_fspace_t*)malloc(SHMEMIO_FSPACE_NALLOC * sizeof(shmemio_fspace_t));
    proc.io.nfspaces = SHMEMIO_FSPACE_NALLOC;
    
    for (int idx = 0; idx < proc.io.nfspaces; idx++) {
      fspace_reset(idx);
    }
    return 0;
  }
  
  for (int idx = 0; idx < proc.io.nfspaces; idx++) {
    if (!proc.io.fspaces[idx].valid) {
      return idx;
    }
  }

  shmemio_fspace_t *new_fspaces =
    (shmemio_fspace_t*)realloc(proc.io.fspaces, (proc.io.nfspaces + SHMEMIO_FSPACE_NALLOC) * sizeof(shmemio_fspace_t));

  if (new_fspaces == NULL)
    return SHMEM_NULL_FSPACE;

  int ret = proc.io.nfspaces;
  proc.io.fspaces = new_fspaces;
  proc.io.nfspaces += SHMEMIO_FSPACE_NALLOC;
  
  for (int idx = ret; idx < proc.io.nfspaces; idx++) {
    fspace_reset(idx);
  }
  
  return ret;
}


/*
 * Set the fspace valid after successful build
 */
static inline void
fspace_set_valid (shmemio_fspace_t* fio)
{
  fio->valid = 1;
  for (int idx = fio->fpe_start; idx < fio->fpe_end; idx++) {
    proc.io.fpes[idx].valid = 1;
  }
}


int
shmemio_fill_regions(shmemio_fspace_t* fio, int start, int rmax)
{
  shmemio_log(trace, "Fspace fill region %d:%d\n", start, rmax-1);
  
  for (int idx = start; idx < rmax; idx++) {
    shmemio_log(trace, "Fspace fill region %d:%d\n",
		idx, rmax-1);
  
    if ( client_region_fill(fio, &(fio->l_regions[idx])) != 0 ) {
      shmemio_log(error, "failed to add fspace region %d\n", idx);
      return -1;
    }
  }
  
  return 0;
}

/*
 * Build all required structures to let the fspace be accessed remotely
 * This has to happen after getting data from server connection
 */
int
shmemio_fill_fspace(int fid)
{
  shmemio_fspace_t* fio = &proc.io.fspaces[fid];

  for (int idx = fio->fpe_start; idx < fio->fpe_end; idx++) {
    shmemio_log(trace, "Fspace %d fill fpe %d of [%d:%d]\n",
		fid, idx, fio->fpe_start, fio->fpe_end);
  
    if ( fpe_fill(&proc.io.fpes[idx], fio) != 0 ) {
      shmemio_log(error, "failed to create fpe endpoints for fpe %d\n", idx);
      return -1;
    }
  }

  // These must be filled in before attempt to unpack region keys
  for (int idx = fio->fpe_start; idx < fio->fpe_end; idx++) {
    proc.comms.eps[fpe_to_pe_index(idx)] = proc.io.fpes[idx].server_ep;
  }

  if (shmemio_fill_regions(fio, 0, fio->nregions) != 0) {
    return -1;
  }
  
  fspace_set_valid(fio);
  
  return 0;
}


