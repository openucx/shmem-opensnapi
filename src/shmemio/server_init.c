/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#include "shmemio.h"
#include "shmemio_server.h"

#include "shmemio_test_util.h"
#include "shmemio_stream_util.h"

#ifdef ENABLE_DEBUG
int shmemio_log_level = 0;

void shmemio_set_loglvl(char *lvl)
{
  if (strcmp(lvl, "error") == 0) {
    shmemio_set_log_level(error);
    return;
  }
  if (strcmp(lvl, "warn") == 0) {
    shmemio_set_log_level(warn);
    return;
  }
  if (strcmp(lvl, "info") == 0) {
    shmemio_set_log_level(info);
    return;
  }
  if (strcmp(lvl, "trace") == 0) {
    shmemio_set_log_level(trace);
    return;
  }

  fprintf(stderr, "Invalid option requested for shmemio log level: %s\n", lvl);
}

#else

void shmemio_set_loglvl(char *lvl)
{
  fprintf (stderr, "Cannot set log level unless compiled to enable debug mode\n");
}

#endif

static inline int
shmemio_unit_size_check(shmemio_server_t *srvr, int unit_size)
{
  if ((unit_size < 0) || (unit_size > srvr->sys_pagesize) || ((srvr->sys_pagesize % unit_size) != 0)) {
    shmemio_log(trace,
		"unit size %u must be >0 divisor of system pagesize %u\n",
		(unsigned)unit_size, (unsigned)srvr->sys_pagesize);
    return -1;
  }
  return 0;
}


static inline int
shmemio_region_len_check(shmemio_server_t *srvr, size_t len)
{
  if ((len % srvr->sys_pagesize) != 0) {
    shmemio_log(trace,
		"region size %u must be multiple of system pagesize %u\n",
		(unsigned)len, (unsigned)srvr->sys_pagesize);
    return -1;
  }
  return 0;
}

/******************************************************************************/
/* Region memory allocate/deallocate functions
/******************************************************************************/

#define shmemio_region_mbase(_reg_) (_reg_->sfpe_mems[0].base)

static inline void
shmemio_init_region_malloc(shmemio_server_region_t* reg)
{
  // It doesn't matter which of these memories we use for malloc
  // We just need the right size area 
  reg->mem_space = create_mspace_with_base((void*)shmemio_region_mbase(reg), reg->mem_len, 1);
}

static inline void
shmemio_finalize_region_malloc(shmemio_server_region_t* reg)
{
  destroy_mspace(reg->mem_space);
}

void
shmemio_region_mallinfo(mallinfo_t *mi, shmemio_server_region_t* reg)
{
  *mi = mspace_mallinfo(reg->mem_space);
}

int
shmemio_region_malloc(shmemio_server_region_t* reg, size_t size, size_t *offset)
{
  void *addr = mspace_malloc(reg->mem_space, size);
  if (addr == NULL)
    return -1;
  
  *offset = (size_t)addr - shmemio_region_mbase(reg);
  return 0;
}

int
shmemio_region_realloc(shmemio_server_region_t* reg, size_t size, size_t *offset)
{
  void *old_addr = (void*)(shmemio_region_mbase(reg) + *offset);
  void *addr = mspace_realloc(old_addr, reg->mem_space, size);
  if (addr == NULL)
    return -1;

  *offset = (size_t)addr - shmemio_region_mbase(reg);
  return 0;
}

int
shmemio_region_realloc_in_place(shmemio_server_region_t* reg, size_t size, size_t *offset)
{
  void *old_addr = (void*)(shmemio_region_mbase(reg) + *offset);
  void *addr = mspace_realloc_in_place(old_addr, reg->mem_space, size);
  if (addr == NULL)
    return -1;

  *offset = (size_t)addr - shmemio_region_mbase(reg);
  return 0;
}

void
shmemio_region_free(shmemio_server_region_t* reg, size_t offset)
{
  void *addr = (void*)(reg->sfpe_mems[0].base + offset);
  mspace_free(reg->mem_space, addr);
}

/******************************************************************************/
/* Region meta-data management
/******************************************************************************/

static inline void
shmemio_release_region(shmemio_server_region_t* reg, ucp_context_h context)
{
  shmemio_finalize_region_malloc(reg);

  if (reg->sfpe_mems != NULL) {
  
#ifdef SHMEMIO_SINGLE_SERVER_PROCESS
    // Single process server release local pmem resources
    for ( int idx = 0; idx < reg->sfpe_size; idx++ ) {
      shmemio_release_sfpe_mem(&(reg->sfpe_mems[idx]), context);
    }
#else
    // Multiprocess server will have leader process tell servants
    // to release their pmem resources
#error "Not implemented"
#endif    
    
    free(reg->sfpe_mems);
  }

  reg->sfpe_mems = NULL;
  reg->sfpe_size = 0;
}

static inline int
shmemio_release_regions(shmemio_server_t *srvr)
{
  if (srvr->regions != NULL) {
    for (int idx = 0; idx < srvr->nregions; idx++) {
      shmemio_release_region(&(srvr->regions[idx]), srvr->context);
    }

    free(srvr->regions);
  }
  
  srvr->maxregions = 0;
  srvr->nregions = 0;
  srvr->regions = NULL;
}

//TODO: each file gets a new region, with part file names set by sfile key
int
shmemio_new_server_region(shmemio_server_t *srvr, const char *sfile_key,
			  size_t len, int unit_size,
			  int sfpe_start, int sfpe_stride, int sfpe_size)
{
  const int nalloc = 8;

  shmemio_log(info, "Adding new region (file key %s), len = %lu, unit = %d, (start,stride,size) = (%d,%d,%d)\n", sfile_key,
	      (long unsigned)len, unit_size, sfpe_start, sfpe_stride, sfpe_size);
  
  shmemio_log_jmp_if(error, err,
		     shmemio_region_len_check(srvr, len) != 0,
		     "add server region len error\n");

  shmemio_log_jmp_if(error, err,
		     shmemio_unit_size_check(srvr, unit_size) != 0,
		     "add server region unit size error\n");

  /**** Extend the array of regions if need be ****/
  if (srvr->regions == NULL) {
    srvr->regions =
      (shmemio_server_region_t*)malloc(sizeof(shmemio_server_region_t) * nalloc);
    shmemio_assert(srvr->regions != NULL, "failed to allocate server regions\n");
    
    srvr->maxregions = nalloc;
    srvr->nregions = 0;
  }

  if (srvr->nregions == srvr->maxregions) {
    shmemio_server_region_t *newreg = 
      (shmemio_server_region_t*)realloc(srvr->regions,
					sizeof(shmemio_server_region_t) * nalloc);
    shmemio_assert(newreg != NULL, "failed to realloc server regions\n");
    
    srvr->regions = newreg;
    srvr->maxregions += nalloc;
  }

  /**** Add the new region at the end of the array ****/
  int rdx = srvr->nregions;
  srvr->nregions++;

  shmemio_server_region_t *reg = &(srvr->regions[rdx]);

  reg->sfpe_start = sfpe_start;
  reg->sfpe_stride = sfpe_stride;
  reg->sfpe_size = sfpe_size;
  reg->unit_size = unit_size;
  reg->mem_len = len;

  reg->sfpe_mems =
    (shmemio_sfpe_mem_t*)malloc(sizeof(shmemio_sfpe_mem_t) * reg->sfpe_size);
  shmemio_log_jmp_if(error, err_release, reg->sfpe_mems == NULL,
		     "failed to allocate sfpe memories array\n");

#ifdef SHMEMIO_SINGLE_SERVER_PROCESS
  // Single process server allocation local memory resources
  for (int idx = 0; idx < sfpe_size; idx++) {
    size_t len = shmemio_init_sfpe_mem(&(reg->sfpe_mems[idx]),
				       srvr->context,
				       reg->mem_len,
				       sfile_key, idx);
    shmemio_log_jmp_if(error, err_release, len != reg->mem_len,
		       "failed to init sfpe %d memory for %s\n", idx, sfile_key);
  }
#else
  // Multiprocess server leader will aggregate information from all processes,
  // where each server file PE (sfpe) represents some rdma accessible memory
  // *** where all sfpe memories of equal length reg->mem_len ***
  // on some remote host. There might be multiple sfpe per remote host.
  //
  // This can look like a loop calling to some routine init_remote_sfpe_mem
  // which will trigger shmemio_init_sfpe_mem on the remote host
  #error "Not implemented"
#endif
  
  shmemio_init_region_malloc(reg);
  
  return rdx;
  
 err_release:
  shmemio_release_region(reg, srvr->context);
  srvr->nregions--;

 err:
  return -1;
      
}

static inline void
shmemio_release_sfpes(shmemio_server_t *srvr)
{
  if (srvr->sfpes != NULL) {

#ifdef SHMEMIO_SINGLE_SERVER_PROCESS
    // Single process server releases local network resources
    for (int idx = 0; idx > srvr->nsfpes; idx++) {
      if (srvr->sfpes[idx].worker_addr != NULL) {
	ucp_worker_release_address(srvr->worker, srvr->sfpes[idx].worker_addr);
      }
    }
#else
    // Multi process server will either be servant or leader.
    // Leader notifies servants to release and disconnect
    // Servants release local resources
#error "Not implemented"
#endif
    
    free(srvr->sfpes);
  }
  
  srvr->sfpes = NULL;
  srvr->nsfpes = 0;
}

static inline int
shmemio_init_sfpes(shmemio_server_t *srvr)
{
  ucs_status_t s;

  shmemio_log(trace, "Server initialize %d sfpes\n", srvr->nsfpes);
  
  srvr->sfpes =
    (shmemio_server_fpe_t*)malloc(srvr->nsfpes * sizeof(shmemio_server_fpe_t));

  shmemio_log_ret_if(error, -1,
		     srvr->sfpes == NULL,
		     "Failed to allocate server fpe array size %z\n",
		     srvr->nsfpes);

  for (int idx = 0; idx < srvr->nsfpes; idx++) {
    shmemio_server_fpe_t *sfpe = &(srvr->sfpes[idx]);

#ifdef SHMEMIO_SINGLE_SERVER_PROCESS
    // All server fpe memories are allocated locally on this server worker
    s = ucp_worker_get_address(srvr->worker,
			       &sfpe->worker_addr,
			       &sfpe->worker_addr_len);
#else
    // Multiprocess server will aggregate worker address info for each
    // remote server file PE (sfpe) on any connected servant processes
#error "Not implemented"
#endif

    shmemio_log(info, "Got sfpe %d worker addr %p worker_addr_len %u\n",
		idx, sfpe->worker_addr, (unsigned)sfpe->worker_addr_len);

    if (s != UCS_OK) {
      sfpe->worker_addr = NULL;
      sfpe->worker_addr_len = 0;
      shmemio_log(error, "can't get sfpe worker address\n");
      return -1;
    }
  }

  return 0;
}


int
shmemio_init_server(shmemio_server_t *srvr, ucp_context_h context,
		    ucp_worker_h worker, int nsfpes,
		    shmemio_conn_cb_t cb, void *args,
		    uint16_t port, size_t sys_pagesize,
		    size_t default_len, size_t default_unit)
{
  shmemio_log(trace, "Initialize shmemio server\n");

#ifndef SHMEMIO_SINGLE_SERVER_PROCESS
  // Multiprocess server will check if it is a metadata/leader process,
  // in which case it does all of the following initialization
  // or if it is just a data/servant process, in which case it will
  // do minimal init, then send info to some master process and wait for commands
#error "Not implemented"
#endif

  int ret;
  
  srvr->context   = context;
  srvr->worker    = worker;

  srvr->nsfpes    = nsfpes;
  srvr->sfpes     = NULL;

  srvr->nregions   = 0;
  srvr->maxregions = 0;
  srvr->regions    = NULL;

  srvr->nfiles         = 0;
  srvr->l_file_hash    = kh_init(str2ptr);
  srvr->cli_conn_hash  = kh_init(ptr2ptr);
  
  srvr->listener  = NULL;

  shmemio_mutex_init(&(srvr->cli_conn_ls_lock), NULL);
  srvr->cli_conns = NULL;

  shmemio_mutex_init(&(srvr->req_conn_ls_lock), NULL);
  srvr->req_conns = NULL;

  srvr->conn_cb_f    = cb;
  srvr->conn_cb_args = args;

  //shmemio_mutex_init(&(srvr->status_lock));
  srvr->status       = shmemio_server_err;
  
  srvr->port         = port;
  srvr->sys_pagesize = sys_pagesize;
  srvr->default_unit = default_unit;
  srvr->default_len  = default_len;

  shmemio_log_jmp_if(warn, err,
		     shmemio_region_len_check(srvr, default_len) != 0,
		     "default_len error\n");
  
  shmemio_log_jmp_if(warn, err,
		     shmemio_unit_size_check(srvr, default_unit) != 0,
		     "default_unit size error\n");

  ret = shmemio_init_sfpes(srvr);
  shmemio_log_jmp_if(error, err,
		     ret != 0, "fail to init server sfpes\n");

  ret = shmemio_new_server_region(srvr, "default_region",
				  srvr->default_len, srvr->default_unit,
				  0, 1, srvr->nsfpes);
  shmemio_log_jmp_if(error, err_sfpes,
		     ret != 0, "Failed to create default region\n");
  
  srvr->status = shmemio_server_init;
  return 0;
  
 err_sfpes:
  shmemio_release_sfpes(srvr);
      
 err:
  return -1;
}
  
int
shmemio_finalize_server(shmemio_server_t *srvr)
{
  int ret = 0;

#ifndef SHMEMIO_SINGLE_SERVER_PROCESS
  // Multiprocess server will have servant process that waits here
  // for the leader process to finalize. Or this function is triggered by
  // leader shutting down
#error "Not implemented"
#endif  
  
  srvr->status = shmemio_server_err;
  ucp_worker_signal(srvr->worker);

  shmemio_release_all_conns(srvr);
  shmemio_release_all_sfiles(srvr);
  
  if (srvr->listener != NULL) {
    ucp_listener_destroy(srvr->listener);
    srvr->listener = NULL;
  }
  
  shmemio_release_regions(srvr);
  shmemio_release_sfpes(srvr);

  shmemio_mutex_destroy(&(srvr->cli_conn_ls_lock));
  shmemio_mutex_destroy(&(srvr->req_conn_ls_lock));
  //shmemio_mutex_destroy(&(srvr->status_lock));

  kh_destroy(str2ptr, srvr->l_file_hash);
  kh_destroy(ptr2ptr, srvr->cli_conn_hash);
  
  return ret;
}
