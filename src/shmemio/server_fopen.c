/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#include "shmemio.h"
#include "shmem/defs_shmemio.h"
#include "shmemio_server.h"
#include "shmemio_test_util.h"
#include "shmemio_stream_util.h"

static inline shmemio_fp_req_t*
get_fpreq(shmemio_req_t* req) {
  return (shmemio_fp_req_t*)req->payload;
}

static inline void
shmemio_pack_data(int req_type, shmemio_sfile_t *sfile, shmemio_fp_req_t *fpreq)
{
  if (req_type == shmemio_ftrunc_req) {
    sfile->blocking_data = (uint64_t)fpreq->size;
  }
  else {
    sfile->blocking_data = 0;
  }
}

static inline void
shmemio_unpack_data(int req_type, shmemio_sfile_t *sfile, shmemio_fp_req_t *fpreq)
{
  if (req_type == shmemio_ftrunc_req) {
    fpreq->size = sfile->blocking_data;
  }
}

static inline int
shmemio_do_ftrunc(shmemio_server_t *srvr, shmemio_fp_req_t *fpreq, int extend_only)
{
  shmemio_sfile_ls_t *snode = (shmemio_sfile_ls_t *)fpreq->fkey;
  shmemio_sfile_t *sfile = snode->sfile;

  if (extend_only && (fpreq->size < sfile->size)) {
    goto ftrunc_success;
  }

  if ((fpreq->size < sfile->size) && (sfile->open_count > 1)) {
    return shmemio_err_shared_resize;
  }

  size_t new_offset = sfile->offset;
  if (shmemio_region_realloc_in_place( &(srvr->regions[sfile->region_id]),
				       fpreq->size,
				       &new_offset ) == 0) {
    sfile->size = fpreq->size;
    goto ftrunc_success;
  }

  //Cannot try realloc with relocation if there is more than one
  //with file open or the flag to alloc relocation is not set
  if ((sfile->open_count > 1) || ((fpreq->ioflags & SHMEM_IO_RELOC) == 0)) {      
    return shmemio_err_resize_norelo;
  }

  if (shmemio_region_realloc( &(srvr->regions[sfile->region_id]),
			      fpreq->size,
			      &new_offset ) == 0) {
    sfile->size = fpreq->size;
    sfile->offset = new_offset;
    fpreq->offset = new_offset;
    goto ftrunc_success;
  }

  return shmemio_err_resize;

 ftrunc_success:
  time(&sfile->mtime);
  return shmemio_success;
  
}

static inline size_t
shmemio_read_sfpe_bytes(shmemio_sfpe_mem_t *sm, size_t offset, FILE *fp, size_t size)
{
  shmemio_log(trace, "Reading %lu bytes starting at %x+%x=%x\n",
	      (long unsigned)size, sm->base, offset, sm->base + offset);
  
  char *addr = (void*)(sm->base + offset);
  return fread(addr, 1, size, fp);
}

static inline size_t
shmemio_write_sfpe_bytes(shmemio_sfpe_mem_t *sm, size_t offset, FILE *fp, size_t size)
{
  shmemio_log(trace, "Writing %lu bytes starting at %x+%x=%x\n",
	      (long unsigned)size, sm->base, offset, sm->base + offset);
  
  char *addr = (void*)(sm->base + offset);
  return fwrite(addr, 1, size, fp);
}

static inline int
shmemio_rw_from_path(shmemio_server_t *srvr, shmemio_sfile_t *sfile, int do_write)
{
  shmemio_server_region_t *reg = &(srvr->regions[sfile->region_id]);
  size_t sym_offset = sfile->offset;
  size_t file_offset = 0;
  int ret = 0;

  FILE *fp = fopen(sfile->sfile_key+1, "r+");
  if (fp == NULL) {
    if (!do_write) {
      shmemio_log(info, "Skipping file read of path %s, open read only failed\n",
		  sfile->sfile_key+1);
      return -1;
    }
    
    fp = fopen(sfile->sfile_key+1, "w+");
    if (fp == NULL) {
      shmemio_log(error, "Failed to open existing or create new backing file %s\n", sfile->sfile_key+1);
      return -1;
    }
  }

  shmemio_log(info, "RW [%s] %lu bytes of data from file path %s\n",
	      do_write ? "write" : "read", sfile->size, sfile->sfile_key+1);
  
  while (file_offset < sfile->size) {
    for (int idx = 0; idx < reg->sfpe_size; idx++) {
      size_t bytes;

      if (do_write) {
	shmemio_write_sfpe_bytes(&(reg->sfpe_mems[idx]), sym_offset, fp, reg->unit_size);
      }
      else {
	shmemio_read_sfpe_bytes(&(reg->sfpe_mems[idx]), sym_offset, fp, reg->unit_size);
      }
      
      file_offset += bytes;

      if (bytes < reg->unit_size) {
	if (file_offset < sfile->size) {
	  ret = -1;
	}
	goto close_and_done;
      }
    }
    sym_offset += reg->unit_size;
  }

 close_and_done:
  fclose(fp);
  return ret;
}

static inline int
shmemio_read_from_path(shmemio_server_t *srvr, shmemio_sfile_t *sfile)
{
  shmemio_log(info, "Reading in all of file %s from slow store to file space\n", sfile->sfile_key);
  return shmemio_rw_from_path(srvr, sfile, 0);
}

static inline int
shmemio_write_to_path(shmemio_server_t *srvr, shmemio_sfile_t *sfile)
{
  shmemio_log(info, "Writing out all of file %s from file space to slow store\n", sfile->sfile_key);
  return shmemio_rw_from_path(srvr, sfile, 1);
}

static inline void
shmemio_flush_sfpe_bytes(shmemio_sfpe_mem_t *sm, size_t offset,
			size_t size, int ioflags)
{
  shmemio_log(info, "Flushing %lu bytes starting at %x+%x=%x\n",
	      (long unsigned)size, sm->base, offset, sm->base + offset);
  
  const void *addr = (void*)(sm->base + offset);
  shmemio_flush_to_persist(addr, size);
}

static inline void
shmemio_flush_region_bytes(shmemio_server_region_t *reg, size_t offset,
			   size_t size, int ioflags)
{
  for (int idx = 0; idx < reg->sfpe_size; idx++) {
    shmemio_flush_sfpe_bytes(&(reg->sfpe_mems[idx]), offset, size, ioflags);
  }
}

static inline int
shmemio_do_flush(shmemio_server_t *srvr, shmemio_fp_req_t *fpreq)
{
  shmemio_sfile_ls_t *snode = (shmemio_sfile_ls_t *)fpreq->fkey;
  shmemio_sfile_t *sfile = snode->sfile;

  time(&sfile->ftime);
  shmemio_flush_region_bytes(&(srvr->regions[sfile->region_id]),
			     sfile->offset,
			     sfile->size / srvr->regions[sfile->region_id].sfpe_size,
			     fpreq->ioflags);
  return shmemio_success;
}



void
shmemio_flush_fspace(shmemio_server_t *srvr, int ioflags)
{
  for (int idx = 0; idx < srvr->nregions; idx++) {
    shmemio_flush_region_bytes(&(srvr->regions[idx]),
			       0,
			       srvr->regions[idx].mem_len,
			       ioflags);
  }
}

static inline int
shmemio_trigger_unblock_actions(shmemio_server_t* srvr, shmemio_sfile_t *sfile, int skiplast);

static inline int
shmemio_conn_close_file(shmemio_server_t* srvr, shmemio_sfile_ls_t *snode, shmemio_conn_t *conn, int no_trigger, int dealloc)
{
  shmemio_log(trace, "Close file %s by ep %p, no_trigger? %d, dealloc? %d\n",
	      snode->sfile->sfile_key, conn->ep, no_trigger, dealloc);
  
  if (snode->next != NULL) {
    snode->next->prev = snode->prev;
  }
  if (snode->prev != NULL) {
    snode->prev->next = snode->next;
  }
  if (conn->open_sfiles == snode) {
    conn->open_sfiles = snode->next;
  }

  shmemio_sfile_t *sfile = snode->sfile;
  sfile->open_count--;
  free(snode);

  shmemio_log_sfile(info, *(sfile), "closed file");

  if (no_trigger == 0) {
    shmemio_trigger_unblock_actions(srvr, sfile, 0);
  }

  // Right now there is no way to reopen files that have no backing file
  // So they will get lost if all apps close them
  if ( ((sfile->open_count == 0) && (!sfile->has_backing_file)) || (dealloc && (sfile->open_count == 0)) ) {
    sfile->mark_for_unload = 1;
    shmemio_log_sfile(trace, *(sfile), "marked for unloading");
  }
  
  return shmemio_success;
}

static inline int
shmemio_conn_close_file_req(shmemio_server_t* srvr, shmemio_fp_req_t *fpreq, int no_trigger)
{
  shmemio_sfile_ls_t *snode = (shmemio_sfile_ls_t *)fpreq->fkey;
  shmemio_conn_t *conn = snode->conn;

  return shmemio_conn_close_file(srvr, snode, conn, no_trigger, fpreq->ioflags & SHMEM_IO_DEALLOC);
}

void
shmemio_conn_close_all_files(shmemio_server_t* srvr, shmemio_conn_t *conn)
{
  while(conn->open_sfiles != NULL) {
    shmemio_conn_close_file(srvr, conn->open_sfiles, conn, 0, 0);
  }
}

static inline void
shmemio_conn_open_file(shmemio_server_t* srvr, ucp_ep_h ep, shmemio_sfile_t *sfile, shmemio_fopen_req_t *foreq)
{
  shmemio_log(trace, "Open file %s by ep %p\n", sfile->sfile_key, ep);

  sfile->open_count++;
  shmemio_conn_t *conn = shmemio_ep_to_conn(srvr, ep);

  shmemio_sfile_ls_t *snode = malloc(sizeof(shmemio_sfile_ls_t));
  snode->sfile = sfile;
  snode->conn = conn;
  snode->waitcond = 0;
  snode->prev = NULL;
  snode->next = conn->open_sfiles;
  conn->open_sfiles = snode;
  if (snode->next != NULL) {
    snode->next->prev = snode;
  }

  foreq->fkey = (uint64_t)snode;

  shmemio_log_sfile(info, *(snode->sfile), "opened file");
}

static inline int
shmemio_do_file_act(int req_type, shmemio_server_t* srvr, shmemio_fp_req_t *fpreq, int no_trigger)
{
  switch (req_type) {
  case shmemio_fclose_req:
    return shmemio_conn_close_file_req(srvr, fpreq, no_trigger);
    
  case shmemio_fp_flush_req:
    return shmemio_do_flush(srvr, fpreq);
    
  case shmemio_ftrunc_req:
    return shmemio_do_ftrunc(srvr, fpreq, 0);
  };

  shmemio_log(error, "Request to do unknown blocking request type %d\n", req_type);
  return shmemio_err_unknown;
};


static inline int
shmemio_do_nb_file_act(int req_type, shmemio_server_t* srvr, shmemio_fp_req_t *fpreq)
{
  switch (req_type) {
  case shmemio_fextend_req:
    return shmemio_do_ftrunc(srvr, fpreq, 1);
  };

  shmemio_log(error, "Request to do unknown nonblocking request type %d\n", req_type);
  return shmemio_err_unknown;
}

int
shmemio_try_nonblock_file_act(shmemio_server_t* srvr, int req_type, shmemio_fp_req_t* fpreq, short *status)
{
  *status = shmemio_do_nb_file_act(req_type, srvr, fpreq);
  return 0;
}


static inline int
shmemio_do_unblock(int req_type, shmemio_server_t *srvr, shmemio_sfile_t *sfile, shmemio_fp_req_t *fpreq)
{
  
  int status = shmemio_do_file_act(req_type, srvr, fpreq, 1);
  shmemio_log(info, "Unblock file %s action type %d [%s] return status %d\n",
	      sfile->sfile_key, req_type, shmemio_rt2str(req_type), status);

  if (status == shmemio_success) {
    if (req_type == shmemio_fclose_req) {
      sfile->close_waitc--;

      fpreq->fkey = 0;
      fpreq->offset = 0;
      fpreq->size = 0;
    }
    else {
      sfile->blocking_nonclose = NULL;

      fpreq->offset = sfile->offset;
      fpreq->size = sfile->size;
    }
  }

  return status;
}

static inline int
shmemio_trigger_unblock_actions(shmemio_server_t* srvr, shmemio_sfile_t *sfile, int skiplast)
{
  const int waitc = sfile->close_waitc + (sfile->blocking_nonclose != NULL ? 1 : 0);

  shmemio_log_sfile(trace, *sfile, "attempt trigger unblock actions");
  
  if (sfile->open_count > waitc) {
    return -1;
  }

  if (waitc == 0) {
    return 0;
  }

  int nonclose_inq = 0;
  int status;
  shmemio_req_t req;
  shmemio_fp_req_t *fpreq = get_fpreq(&req);
  fpreq->size = sfile->size;
  fpreq->offset = sfile->offset;

  shmemio_log(trace, "File %s unblocking %d waiters\n", sfile->sfile_key, waitc);

  for (int idx = 0; idx < (waitc - skiplast); idx++) {
    shmemio_sfile_ls_t *snode = (shmemio_sfile_ls_t *)(sfile->iowait[idx]);
    shmemio_log(trace, "Unblock waiter %d, snode %p\n", idx, snode);

    if (snode == sfile->blocking_nonclose) {
      nonclose_inq = 1;
    }
    else {
      fpreq->fkey = (uint64_t)snode;
      fpreq->ioflags = snode->waitcond >> SHMEMIO_REQ_TYPE_BITS;
      req.type = ((1 << SHMEMIO_REQ_TYPE_BITS) - 1) & snode->waitcond;
      snode->waitcond = 0;
      
      shmemio_log(trace, "Unblock req type %d (should be %d), with ioflags %d\n",
		  req.type, shmemio_fclose_req, fpreq->ioflags);

      shmemio_assert(req.type == shmemio_fclose_req,
		     "Request type %d not fclose, snode mismatch blocking_noncloseer",
		     req.type);

      //We are about to close the file and free the snode
      ucp_ep_h ep = snode->conn->ep;
      status = shmemio_do_unblock(req.type, srvr, sfile, fpreq);
      //Endpoint for the connection that opened this file resulting in the snode
      shmemio_send_response(srvr, ep, &req, status);
    }
  }
  
  if (nonclose_inq) {
    shmemio_sfile_ls_t *snode = sfile->blocking_nonclose;

    fpreq->fkey = (uint64_t)snode;
    fpreq->ioflags = snode->waitcond >> SHMEMIO_REQ_TYPE_BITS;
    req.type = ((1 << SHMEMIO_REQ_TYPE_BITS) - 1) & snode->waitcond;
    snode->waitcond = 0;
    
    shmemio_unpack_data(req.type, sfile, fpreq);
			    
    status = shmemio_do_unblock(req.type, srvr, sfile, fpreq);
    shmemio_send_response(srvr, snode->conn->ep, &req, status);
  }
  
  return 0;
}

static inline int
shmemio_block_or_trigger(int req_type, shmemio_server_t *srvr, shmemio_fp_req_t *fpreq)
{
  shmemio_log(trace, "Block or trigger request type %d [%s]\n",
	      req_type, shmemio_rt2str(req_type));
  
  shmemio_sfile_ls_t *snode = (shmemio_sfile_ls_t*)fpreq->fkey;
  shmemio_sfile_t *sfile = snode->sfile;

  if (req_type == shmemio_fclose_req) {
    sfile->close_waitc++;
  }
  else {
    sfile->blocking_nonclose = snode;
    shmemio_pack_data(req_type, sfile, fpreq);
  }

  if (shmemio_trigger_unblock_actions(srvr, sfile, 1) == 0) {
    return shmemio_do_unblock(req_type, srvr, sfile, fpreq);
  }

  const int waitc = sfile->close_waitc + (sfile->blocking_nonclose != NULL ? 1 : 0);

  snode->waitcond = (fpreq->ioflags << SHMEMIO_REQ_TYPE_BITS) | (unsigned)req_type;

  if (waitc >= sfile->iowait_len) {
    shmemio_assert(sfile->open_count > waitc,
		   "%d iowaiters on sfile with %d file open.\n",
		   waitc, sfile->open_count);
    
    int new_len = ((sfile->open_count >> 2) << 2) + 4;
    void** new_iowait = realloc(sfile->iowait, sizeof(void*) * new_len);
    shmemio_assert(new_iowait != NULL, "realloc error for %d void*\n", new_len);

    sfile->iowait = new_iowait;
    sfile->iowait_len = new_len;
  }

  shmemio_log(info,
	      "Add blocking ep %p, ioflags %d, req_type %d, snode %p, to file %s. Wait to close = %d. Wait for nonclose op? %d\n",
	      snode->conn->ep, fpreq->ioflags, req_type, snode, sfile->sfile_key,
	      sfile->close_waitc, (sfile->blocking_nonclose != NULL));

  sfile->iowait[waitc - 1] = (void*)snode;
  return shmemio_action_blocked;
}
			
int
shmemio_try_blocking_file_act(shmemio_server_t* srvr, int req_type, shmemio_fp_req_t* fpreq, short *status)
{
  shmemio_sfile_ls_t *snode = (shmemio_sfile_ls_t*)fpreq->fkey;
  shmemio_sfile_t *sfile = snode->sfile;

  shmemio_log_sfile(info, *sfile, "try blocking file action");
  
  if (fpreq->ioflags & SHMEM_IO_WAIT) {

    if (snode->waitcond != 0) {
      //This open file pointer already blocked on this file
      //The open file pointer must have been shared and some other thread tried
      //to block on it
      *status = shmemio_err_doubleblock;
      return -1;
    }
    if ( (req_type != shmemio_fclose_req) && (sfile->blocking_nonclose != NULL) ) {
      //If more than one open file tries to block and wait for close
      //but is not closing the file, deadlock will occur
      //Don't allow this
      *status = shmemio_err_nodeadlock;
      return -1;
    }
    if (sfile->open_count > 1) {
      //More than one has this file open
      //Either block this request or trigger unblock of all actions if this
      //request causes unblocking actions
      *status = shmemio_block_or_trigger(req_type, srvr, fpreq);
      return 0;
    }
  }

  *status = shmemio_do_file_act(req_type, srvr, fpreq, 0);

  if (sfile->mark_for_unload != 0) {
    shmemio_log_sfile(info, *sfile, "unload sfile");
    shmemio_release_sfile(srvr, sfile);
  }

  return 0;
}

  
static inline int
shmemio_set_loaded_file(shmemio_server_t *srvr, shmemio_sfile_t* sfile)
{
  khint_t k;
  int absent;

  k = kh_put(str2ptr, srvr->l_file_hash, sfile->sfile_key, &absent);
  
  if (absent) {
    kh_val(srvr->l_file_hash, k) = (void*)sfile;
    srvr->nfiles++;
    return 0;
  }

  shmemio_log(info, "Did not map server file object for %s. Already mapped\n", sfile->sfile_key);
  return -1;
}

static inline void*
shmemio_retrieve_loaded_file(shmemio_server_t *srvr, char* sfile_key, int remove)
{
  khint_t k;

  k = kh_get(str2ptr, srvr->l_file_hash, sfile_key);
  if (k == kh_end(srvr->l_file_hash))
    return NULL;

  void *ret = (void*)kh_val(srvr->l_file_hash, k);
  if (remove) {
    kh_del(str2ptr, srvr->l_file_hash, k);
    srvr->nfiles--;
  }
  
  return ret;
}

static inline shmemio_sfile_t*
shmemio_get_loaded_file(shmemio_server_t *srvr, char* sfile_key)
{
  return shmemio_retrieve_loaded_file(srvr, sfile_key, 0);
}

static inline shmemio_sfile_t*
shmemio_unset_loaded_file(shmemio_server_t *srvr, char* sfile_key)
{
  return shmemio_retrieve_loaded_file(srvr, sfile_key, 1);
}

static inline void
shmemio_fopen_from_preloaded(shmemio_server_t *srvr, shmemio_sfile_t *sfile,
			     shmemio_fopen_req_t *foreq)
{
  shmemio_server_region_t *reg = &(srvr->regions[sfile->region_id]);

  foreq->fsize = sfile->size;

  foreq->sfpe_start = reg->sfpe_start;
  foreq->sfpe_stride = reg->sfpe_stride;
  foreq->sfpe_size   = reg->sfpe_size;
  foreq->unit_size  = reg->unit_size;

  foreq->l_region = sfile->region_id;

  foreq->offset = sfile->offset;

  shmemio_log(info, "Opened preloaded file (%p) %s, size %lu on region %d at offset [%x:%x]\n",
	      sfile, sfile->sfile_key, (long unsigned)foreq->fsize, sfile->region_id,
	      foreq->offset, foreq->offset + foreq->fsize);
}


static inline int
shmemio_alloc_on_region(shmemio_server_t *srvr, int regid, shmemio_fopen_req_t *foreq)
{
  shmemio_server_region_t *reg = &(srvr->regions[regid]);
  size_t size_per_sfpe = foreq->fsize / reg->sfpe_size;
  if (size_per_sfpe < reg->unit_size) {
    size_per_sfpe = reg->unit_size;
  }

  if (shmemio_region_malloc(reg, size_per_sfpe, &foreq->offset) != 0)
    return -1;
  
  foreq->unit_size  = reg->unit_size;
  
  foreq->sfpe_start = reg->sfpe_start;
  foreq->sfpe_stride = reg->sfpe_stride;
  foreq->sfpe_size   = reg->sfpe_size;

  foreq->l_region = regid;
  foreq->fsize = size_per_sfpe * reg->sfpe_size;

  shmemio_log(info, "Allocated size %lu * %d = %lu on region %d at offset [%lx:%lx]\n",
	      (long unsigned)size_per_sfpe, reg->sfpe_size, (long unsigned)foreq->fsize,
	      regid, foreq->offset, foreq->offset + foreq->fsize);
  
  return 0;
}

int
shmemio_release_sfile(shmemio_server_t *srvr, shmemio_sfile_t* sfile)
{
  shmemio_log(info, "Release sfile %s, addr %x from region %d\n",
	      sfile->sfile_key, sfile->offset, sfile->region_id);

  if (sfile->has_backing_file) {
    shmemio_write_to_path(srvr, sfile);
  }
  
  shmemio_server_region_t *reg = &(srvr->regions[sfile->region_id]);
  shmemio_region_free(reg, sfile->offset);
  
  free(sfile->sfile_key);
  free(sfile);
}

int
shmemio_release_all_sfiles(shmemio_server_t *srvr)
{
  khint_t k;
  
  for (k = 0; k < kh_end(srvr->l_file_hash); ++k) {
    if (kh_exist(srvr->l_file_hash, k)) {
      shmemio_release_sfile(srvr, kh_val(srvr->l_file_hash, k));
      kh_del(str2ptr, srvr->l_file_hash, k);
    }
  }
}

static inline int
shmemio_fload(shmemio_server_t *srvr, const char *sfile_key,
	      shmemio_fopen_req_t *foreq, short* status)
{
  int ret;

  shmemio_log(info, "Got request to load file %s, size %lu, unit_size %d on pe [%d +%d] by %d\n",
	      sfile_key, (long unsigned) foreq->fsize, foreq->unit_size,
	      foreq->sfpe_start, foreq->sfpe_size, foreq->sfpe_stride);

  //TODO: each file gets its own region presently, but we can enable multiple file
  //per region by adding metatdata header section in pmem file parts
  //Currently pmem part files are named for the file they contain
  //See server_pmem.c for pmem file part creation
#if 0
  // This section tries to allocate a file on an existing region
  for (int idx = 0; idx < srvr->nregions; idx++) {
    shmemio_server_region_t *reg = &(srvr->regions[idx]);
    if ( ((foreq->sfpe_size < 0)   || (reg->sfpe_size == foreq->sfpe_size)) &&
	 ((foreq->sfpe_start < 0)  || (reg->sfpe_start == foreq->sfpe_start)) &&
	 ((foreq->sfpe_stride < 0) || (reg->sfpe_stride == foreq->sfpe_stride)) &&
	 ((foreq->unit_size < 0)  || (reg->unit_size == foreq->unit_size)) ) {

      if (shmemio_alloc_on_region(srvr, idx, foreq) == 0)
	return 0;
    }
  }
#endif

  const int new_reg_stride = ( ((foreq->sfpe_stride < 0) || (foreq->sfpe_stride > srvr->nsfpes)) ?
			       1 : foreq->sfpe_stride );
			       
  const int new_reg_size   = ( ((foreq->sfpe_size < 0) || (foreq->sfpe_size > srvr->nsfpes)) ?
			       1 : foreq->sfpe_size );

  const int max_start = srvr->nsfpes - new_reg_size;

  const int new_reg_start  = ( ((foreq->sfpe_start < 0) || (foreq->sfpe_start > max_start)) ?
			       ( max_start == 0 ? 0 : (rand() % max_start) ) : foreq->sfpe_start );
  
  const size_t min_reg_len = (foreq->fsize / new_reg_size) + (foreq->fsize / new_reg_size / 10);
  
  const size_t new_reg_len = ( (srvr->default_len > min_reg_len) ?
			       srvr->default_len :
			       ((min_reg_len / srvr->sys_pagesize) + 1) * srvr->sys_pagesize );

  //TODO: correct unit size if invalid
  const int new_reg_unit   = (foreq->unit_size < 0) ? srvr->default_unit : foreq->unit_size;


  ret = shmemio_new_server_region(srvr, sfile_key,
				  new_reg_len, new_reg_unit,
				  new_reg_start, new_reg_stride, new_reg_size);

  if (ret < 0) {
    shmemio_log(error, "Failed to make new region to match file open request\n");
    *status = shmemio_err_region_create;
    return -1;
  }

  ret = shmemio_alloc_on_region(srvr, ret, foreq);
  shmemio_assert(ret == 0, "Failed to allocate file on new region\n");

  return 0;
}

int
shmemio_server_fopen(shmemio_server_t *srvr, ucp_ep_h ep,
		     shmemio_fopen_req_t *foreq,
		     short* status)
{
  int ret;
  shmemio_sfile_t *sfile = NULL;
  char *sfile_key        = NULL;
  int has_backing_file   = 0;

  *status = shmemio_err_unknown;
  
  if (foreq->file_path_len > 0) {

    has_backing_file = 1;
    sfile_key = malloc(foreq->file_path_len + 2);
    sfile_key[0] = '@';
  
    ret = shmemio_streamrecv(srvr->worker, ep, &(sfile_key[1]), foreq->file_path_len);
    shmemio_log_jmp_if(error, err_file, ret != foreq->file_path_len, "Failed to recv fopen file path\n");

    sfile_key[foreq->file_path_len + 1] = '\0';

    shmemio_log(info, "Got request to open file %s, size %lu, unit_size %d on pe [%d +%d] by %d\n",
		sfile_key, (long unsigned)foreq->fsize, foreq->unit_size,
		foreq->sfpe_start, foreq->sfpe_size, foreq->sfpe_stride);
    
    sfile = shmemio_get_loaded_file(srvr, sfile_key);
  }
  
  if (sfile != NULL) {
    free(sfile_key);
    shmemio_fopen_from_preloaded(srvr, sfile, foreq);
    time(&(sfile->atime));
    shmemio_log_sfile(info, *sfile, "opened preloaded");
  }
  else {
    sfile = malloc(sizeof(shmemio_sfile_t));
    shmemio_assert(sfile != NULL, "malloc error");
    
    if ( has_backing_file == 0 ) {
      uint64_t fval = (uint64_t)sfile;
      const size_t strbytes = sizeof(uint64_t) * 2;
      
      sfile_key = malloc(strbytes + 2);
      sfile_key[0] = '!';
      
      for(int i = 0; i < strbytes; i++) {
	snprintf(sfile_key + i + 1, 2, "%02x", fval & 0xFF);
	fval >>= 8;
      }
      
      sfile_key[strbytes + 1] = '\0';
    }
    
    ret = shmemio_fload(srvr, sfile_key, foreq, status);
    shmemio_log_jmp_if(error, err_file, ret < 0, "File load failure\n");
    
    sfile->region_id        = foreq->l_region;
    sfile->sfile_key        = sfile_key;
    sfile->size             = foreq->fsize;
    sfile->offset           = foreq->offset;
    sfile->has_backing_file = has_backing_file;
    sfile->mark_for_unload  = 0;
    sfile->open_count       = 0;
    sfile->close_waitc      = 0;
    sfile->blocking_nonclose = NULL;
    sfile->blocking_data    = 0;
    sfile->iowait           = NULL;
    sfile->iowait_len       = 0;
    time(&(sfile->ctime));
    memcpy(&(sfile->atime), &(sfile->ctime), sizeof(time_t));
    memcpy(&(sfile->mtime), &(sfile->ctime), sizeof(time_t));
    memcpy(&(sfile->ftime), &(sfile->ctime), sizeof(time_t));

    if (has_backing_file) {
      //This silently fails if backing file does not exist
      //Behavior is expected since we don't want to read in garbage
      //over persistent data
      shmemio_read_from_path(srvr, sfile);
    }
    
    ret = shmemio_set_loaded_file(srvr, sfile);
    shmemio_assert(ret == 0, "Failed to add loaded file to lookup hash\n");
    shmemio_log_sfile(info, *sfile, "opened newly loaded");
  }
  
  shmemio_conn_open_file(srvr, ep, sfile, foreq);
  *status = shmemio_success;
  return 0;
    
 err_file:
  free(sfile_key);
  free(sfile);
  return -1;
}
  
  
