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
#include "shmemio_stream_util.h"

#include <string.h>    /* memset */
#include <stdlib.h>    /* atoi */

/*
 * Initialize shmem process to be a client for shmemio API
 */
void
shmemio_init_client()
{
  shmemio_assert((proc.io.nfpes < 0),
		 "shmemio_init called and already initialized");
	   
  proc.io.nfpes = 0;
  proc.io.fpes = NULL;
  proc.io.nfspaces = 0;
  proc.io.fspaces = NULL;

#ifdef ENABLE_DEBUG
  proc.io.fp_active_list = NULL;
  shmemio_set_log_level(warn);
#endif
}

#ifdef ENABLE_DEBUG

/*
 * Add open files in an active list on the client for debugging
 */
static inline void
shmemio_fp_active_list_push( shmemio_fp_t *fp ) {
  shmemio_assert((fp != NULL), "Tried to push null fp into active list\n");
  
  shmemio_fp_t *old_head = proc.io.fp_active_list;
  fp->next_active = old_head;
  if (old_head != NULL) {
    old_head->prev_active = fp;
  }
  proc.io.fp_active_list = fp;
}

/*
 * Remove closed files in an active list on the client for debugging
 */
static inline void
shmemio_fp_active_list_remove( shmemio_fp_t *fp ) {
  shmemio_assert((fp != NULL), "Tried to remove null fp from active list\n");
  
  if (fp == proc.io.fp_active_list) {
    proc.io.fp_active_list = fp->next_active;
  }
  else {
    shmemio_assert((fp->prev_active != NULL), "null prev pointer on not the list head\n");
    fp->prev_active->next_active = fp->next_active;
  }

  if (fp->next_active != NULL) {
    fp->next_active->prev_active = fp->prev_active;
  }
  
  fp->next_active = NULL;
  fp->prev_active = NULL;
}
#endif /* ENABLE_DEBUG */


/*
 * Get a new file pointer object
 */
static inline shmemio_fp_t*
shmemio_get_new_fp() {
  
  shmemio_fp_t *ret = (shmemio_fp_t*)malloc(sizeof(shmemio_fp_t));
  
#ifdef ENABLE_DEBUG
  shmemio_fp_active_list_push(ret);
#endif
  
  return ret;
}

/*
 * Free an unused file pointer object
 */
static inline void
shmemio_fp_release(shmemio_fp_t *fp) {
  
#ifdef ENABLE_DEBUG
  shmemio_fp_active_list_remove(fp);
#endif

  free(fp);
}


/*
 * Finalize shmem process that is a client for shmemio API
 */
void
shmemio_finalize_client()
{
  for (int idx = 0; idx < proc.io.nfspaces; idx++) {
    if (proc.io.fspaces[idx].valid) {
      shmemio_log(warn, "Found open fspace connection in finalize. Disconnecting..\n");
      shmem_disconnect(idx);
    }
  }
  
  if (proc.io.fspaces != NULL)
    free(proc.io.fspaces);
  proc.io.nfspaces = 0;

  if (proc.io.fpes != NULL)
    free(proc.io.fpes);
  proc.io.nfpes = 0;

#ifdef ENABLE_DEBUG
  while(proc.io.fp_active_list != NULL) {
    shmemio_fp_t *fp = proc.io.fp_active_list;
    shmemio_log(warn, "open file pointer %p found during finalize client\n", fp);
    shmem_close((shmem_fp_t*)fp, 0);
  }
#endif 
}

/*
 * is the given address in this fspace on this pe?  
 * Non-zero if yes, 0 if no.
 */
inline static int
in_fpe_region(uint64_t addr, const shmemio_client_region_t* loc)
{
  return ((loc->l_base <= addr) && (addr < loc->l_end));
}

/*
 * go from global pe index to file pe (fpe) object pointer
 */
inline static shmemio_client_fpe_t*
pe_to_fpe(int pe)
{
  return &(proc.io.fpes[pe_to_fpe_index(pe)]);
}

/*
 * go from file pe (fpe) object to the fspace to which it belongs
 */
inline static shmemio_fspace_t*
fpe_to_fspace(const shmemio_client_fpe_t *fpe)
{
  return &(proc.io.fspaces[fpe->fspace]);
}

/*
 * go from file pointer (fp) object to the fspace to which it belongs
 */
inline static shmemio_fspace_t*
fp_to_fspace(const shmem_fp_t *fp)
{
  return &(proc.io.fspaces[((shmemio_fp_t*)fp)->fspace]);
}

/*
 * Initialize a file operation request object to send to fspace server
 */
inline static shmemio_fp_req_t*
init_fpreq(shmemio_req_t *req, shmem_fp_t *fp, int ioflags)
{
  shmemio_log(trace, "Init fpreq with req %p, fp %p, flags %d\n", req, fp, ioflags);
  
  shmemio_fp_t *fpio = (shmemio_fp_t*)fp;
  shmemio_fp_req_t *fpreq = (shmemio_fp_req_t*)req->payload;
  
  fpreq->offset = fpio->offset;
  fpreq->size = fpio->size;
  fpreq->fkey = fpio->fkey;
  fpreq->ioflags = ioflags;

  req->status = shmemio_err_unknown;
  return fpreq;
}

/*
 * Send and receive a file operation request. Will block the client 
 * until the server sends a request
 */
inline static int
sendrecv_req(shmemio_req_t *req, shmemio_fspace_t *fio)
{
  int ret = shmemio_streamsend(fio->ch->w, fio->req_ep, req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, -1, ret < 0,
		     "Failed to send request type %d\n", req->type);

  ret = shmemio_streamrecv(fio->ch->w, fio->req_ep, req, sizeof(shmemio_req_t));
  shmemio_log(trace, "Receive response to request type %d, status %d\n", req->type, req->status);
  
  shmemio_log_ret_if(error, -1, ret != sizeof(shmemio_req_t),
		     "Failed to recv response type %d\n", req->type);
}


/*
 * Translate a file pe (fpe) on a client region into remote region access
 */
static inline shmemio_remote_region_t*
client_fpe_to_remote_region(const shmemio_client_region_t *l_reg, int fpe)
{
  const int rdx = (fpe - l_reg->fpe_start) / l_reg->fpe_stride;
  
  shmemio_assert( ((rdx >= 0) && (rdx < l_reg->fpe_size)),
		  "fpe %d not in client region [%u:+%u] fpe (%d:%d) by %d\n",
		  fpe, l_reg->l_base, l_reg->l_base + l_reg->len,
		  l_reg->fpe_start, l_reg->fpe_start + l_reg->fpe_size - 1,
		  l_reg->fpe_stride);
  
  return &(l_reg->r_regions[rdx]);
}

/*
 * Translate an address on a file space into a local client region mapping
 */
inline static shmemio_client_region_t *
fspace_addr_to_client_region(const shmemio_fspace_t* fio, uint64_t addr)
{
  for (int idx = 0; idx < fio->nregions; idx++) {
    if (in_fpe_region(addr, &(fio->l_regions[idx])))
      return &(fio->l_regions[idx]);
  }
  return NULL;
}

/*
 * * * * * Translation functions for put/get * * * * *
 */

/*
 * Test if address exists in some fspace assigned to this pe
 */
int
shmemio_addr_accessible(const void *addr, int pe)
{
  shmemio_pe_range_check(pe);
  shmemio_valid_check(pe_to_fpe(pe));
  // Get the file space object from the file pe indicated by this pe number
  const shmemio_fspace_t *fio = fpe_to_fspace(pe_to_fpe(pe));
  shmemio_valid_check(fio);

  // Check if this address maps to an existing client region on the fspace
  return (fspace_addr_to_client_region(fio, (uint64_t)addr) >= 0);
}

/*
 * Lookup rkey and raddr in fspaces. Called when translate fails for heaps.
 */
void
secondary_remote_key_and_addr(uint64_t local_addr, int pe,
			      ucp_rkey_h *rkey_p, uint64_t *raddr_p)
{
  shmemio_pe_range_check(pe);
  // Find the file pe index indicated by this pe number
  const shmemio_client_fpe_t *fpe = pe_to_fpe(pe);
  shmemio_valid_check(fpe);
  // Find the file space object for the file pe
  const shmemio_fspace_t *fio = fpe_to_fspace(fpe);
  shmemio_valid_check(fio);

  // Find the local client region for this address on this fspace
  const shmemio_client_region_t * l_reg =
    fspace_addr_to_client_region(fio, local_addr);

  shmemio_assert(l_reg != NULL, "cannot find region for addr %p\n", local_addr);

  // Find the remote region access for the indicate file pe
  const shmemio_remote_region_t *r_reg =
    client_fpe_to_remote_region(l_reg, pe_to_fpe_index(pe));

  // Calculate offset and remote addr from local region and remote access
  const uint64_t my_offset = local_addr - l_reg->l_base;
  const uint64_t remote_addr = my_offset + r_reg->r_base;

  // Use the remote access key and the calculated address
  *rkey_p = r_reg->rkey;
  *raddr_p = remote_addr;
}

/*
 * * * * * Client API * * * * *
 */

/*
 * Client API: Connect to remote fspace
 */
shmem_fspace_t shmem_connect(shmem_fspace_conx_t *conx)
{
  size_t len;
  
  shmemio_log(info, "shmem_connect to %s:%d\n",
	      conx->storage_server_name, conx->storage_server_port);

  // Get an unused file space object
  shmem_fspace_t fid_ret = shmemio_get_new_fspace();

  // File space object allocation error
  if ( fid_ret == SHMEM_NULL_FSPACE ) {
    shmemio_log(error, "failed to init new fspace %d\n", fid_ret);
    goto err;
  }

  // Use the internal connect function to get access info from server
  if ( shmemio_connect_fspace(conx, fid_ret) != 0 ) {
    shmemio_log(error, "failed to connect and recv remote fspace data from server\n");
    goto err_fspace;
  }

  // Populate all of the file space structures, unpack rkeys, etc
  if ( shmemio_fill_fspace(fid_ret) != 0 ) {
    shmemio_log(error, "failed to build fspace %d data access\n", fid_ret);
    goto err_fspace;
  }

  return fid_ret;
  
 err_fspace:
  shmemio_release_fspace(fid_ret);
  
 err:
  return SHMEM_NULL_FSPACE;
}

/*
 * Client API: Disconnect from a file space
 */
int shmem_disconnect(shmem_fspace_t fspace)
{
  shmemio_log(info, "Disconnect fspace %d\n", fspace);

  shmemio_fspace_range_check(fspace);
  shmemio_fspace_t *fio = &proc.io.fspaces[fspace];
  shmemio_valid_check(fio);
  
  shmemio_req_t req;
  req.type = shmemio_disco_req;

  int ret = shmemio_streamsend(fio->ch->w, fio->req_ep, &req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, -1, ret < 0, "Failed to send disconnect request\n");
  
  shmemio_release_fspace(fspace);

  return req.status;
}

/*
 * Client API: Get statistics for this file
 */
int shmem_fp_stat(shmem_fp_t *fp)
{
  shmemio_req_t req;
  shmemio_fp_req_t *fpreq = init_fpreq(&req, fp, 0);
  req.type = shmemio_fp_stat_req;
  
  shmemio_fspace_t *fio = fp_to_fspace(fp);
  shmemio_fp_stat_t fstat;
  
  int ret = shmemio_streamsend(fio->ch->w, fio->req_ep, &req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, -1, ret < 0, "Failed to send stat request\n");

  ret = shmemio_streamrecv(fio->ch->w, fio->req_ep, &fstat, sizeof(shmemio_fp_stat_t));
  shmemio_log_ret_if(error, -1, ret != sizeof(shmemio_fp_stat_t),
		     "Failed to recv fstat\n")

  fp->size = fstat.size;
  memcpy(&fp->ctime, &(fstat.ctime), sizeof(time_t));
  memcpy(&fp->atime, &(fstat.atime), sizeof(time_t));
  memcpy(&fp->mtime, &(fstat.mtime), sizeof(time_t));
  memcpy(&fp->ftime, &(fstat.ftime), sizeof(time_t));

  shmemio_log_fp(info, ((shmemio_fp_t*)fp), "fstat returned");
  
  return shmemio_success;
}

/*
 * Client API: Extend this file to a larger size
 */
int shmem_fextend(shmem_fp_t *fp, size_t bytes)
{
  shmemio_req_t req;
  shmemio_fp_req_t *fpreq = init_fpreq(&req, fp, 0);
  req.type = shmemio_fextend_req;
  fpreq->size = fp->size + bytes;

  sendrecv_req(&req, fp_to_fspace(fp));
  update_fp_status(&req, fpreq, fp);
  
  return req.status;
}

/*
 * Client API: Flush this file to persistance
 */
int shmem_fp_flush(shmem_fp_t *fp, int ioflags)
{
  shmemio_req_t req;
  shmemio_fp_req_t *fpreq = init_fpreq(&req, fp, ioflags);
  req.type = shmemio_fp_flush_req;

  sendrecv_req(&req, fp_to_fspace(fp));
  update_fp_status(&req, fpreq, fp);
  
  return req.status;
}

/*
 * Client API: Truncate the file
 */
int shmem_ftrunc(shmem_fp_t *fp, size_t bytes, int ioflags)
{
  shmemio_req_t req;
  shmemio_fp_req_t *fpreq = init_fpreq(&req, fp, ioflags);
  req.type = shmemio_ftrunc_req;
  fpreq->size = bytes;

  sendrecv_req(&req, fp_to_fspace(fp));
  return req.status;
}

/*
 * Client API: Close the file
 */
int shmem_close (shmem_fp_t *fp, int ioflags)
{
  shmemio_log_fp(info, ((shmemio_fp_t*)fp), "close file");
  shmemio_req_t req;
  shmemio_fp_req_t *fpreq = init_fpreq(&req, fp, ioflags);
  req.type = shmemio_fclose_req;

  sendrecv_req(&req, fp_to_fspace(fp));
  update_fp_status(&req, fpreq, fp);
  shmemio_fp_release((shmemio_fp_t*)fp);
  
  return req.status;
}


/*
 * Client API: Get statistics for this file space
 */
int shmem_fspace_stat(shmem_fspace_t fspace, shmem_fspace_stat_t *stat)
{
  shmemio_fspace_range_check(fspace);
  shmemio_fspace_t *fio = &proc.io.fspaces[fspace];
  
  if (fio->valid != 1) {
    return shmemio_err_invalid;
  }
  
  shmemio_req_t req;
  req.type = shmemio_fspace_stat_req;

  int ret = shmemio_streamsend(fio->ch->w, fio->req_ep, &req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, shmemio_err_send, ret < 0, "Failed to send stat request\n");

  ret = shmemio_streamrecv(fio->ch->w, fio->req_ep, stat, sizeof(shmem_fspace_stat_t));
  shmemio_log_ret_if(error, shmemio_err_recv, ret != sizeof(shmem_fspace_stat_t),
		     "Failed to recv fspace stat\n");

  stat->pe_start = proc.nranks + fio->fpe_start;
  
  shmemio_log_ret_if(error, shmemio_err_unknown, stat->pe_size != fio->nfpes,
		     "Sanity check failed on fspace stat, returned fpe count %d != %d\n",
		     stat->pe_size, fio->nfpes);
  
  return shmemio_success;
}

/*
 * Client API: Flush the entire file space to persistence
 */
void shmem_fspace_flush(shmem_fspace_t fspace, int ioflags)
{
  shmemio_fspace_range_check(fspace);
  shmemio_fspace_t *fio = &proc.io.fspaces[fspace];
  
  shmemio_req_t req;
  req.type = shmemio_fspace_flush_req;
  ((int*)req.payload)[0] = ioflags;

  sendrecv_req(&req, fio);
}


/*
 * Client API: Open a file
 */
shmem_fp_t *shmem_open(shmem_fspace_t fspace, const char *file, size_t fsize,
		       int pe_start, int pe_stride, int pe_size, int unit_size, int *err)
{
  shmemio_fspace_range_check(fspace);
  shmemio_fspace_t *fio = &(proc.io.fspaces[fspace]);
  
  shmemio_fp_t *fp = shmemio_get_new_fp();
  shmemio_assert(fp != NULL, "Unable to allocate new file pointer object\n");

  // Init the file pointer with the input parameters
  fp->fspace = fspace;
  fp->addr = NULL;
  fp->size = fsize;
  fp->unit_size = unit_size;
  fp->pe_start = pe_start;
  fp->pe_stride = pe_stride;
  fp->pe_size = pe_size;

  // Call the internal client file open
  if (shmemio_client_fopen(fio, file, fp, err) != 0) {
    shmemio_log(error, "File open failed\n");
    goto err_fp;
  }

  shmemio_log_fp(info, fp, "open file");
  return (shmem_fp_t*)fp;

 err_fp:
  shmemio_fp_release(fp);
  
 err:
  return NULL;
}

/*
 * Client API: return a string for error number
 */
void shmem_strerror(int errnum, char *strbuf)
{
  int len;
  const char *str = shmemio_err2str(errnum, &len);
  memcpy(strbuf, str, len); 
}

