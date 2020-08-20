/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef SHMEMIO_FPE_UTIL_H
#define SHMEMIO_FPE_UTIL_H

static inline void
flush_callback(void *request, ucs_status_t status)
{
}

static inline ucs_status_t
flush_ep(ucp_worker_h worker, ucp_ep_h ep)
{
    void *request;
    request = ucp_ep_flush_nb(ep, 0, flush_callback);

    if (request == NULL) {
        return UCS_OK;
    } else if (UCS_PTR_IS_ERR(request)) {
        return UCS_PTR_STATUS(request);
    } else {
        ucs_status_t status;
        do {
            ucp_worker_progress(worker);
            status = ucp_request_check_status(request);
        } while (status == UCS_INPROGRESS);
        ucp_request_release(request);
        return status;
    }
}


static inline void
fpe_ep_destroy(shmemio_client_fpe_t *fpe, shmemio_fspace_t *fio)
{
  ucs_status_t status;
 
  status = flush_ep(fio->ch->w, fpe->server_ep);
  shmemio_log(trace, "ucp_ep_flush is completed with status %d (%s)\n",
	     status, ucs_status_string(status));

  ucp_ep_destroy(fpe->server_ep);
  return;
}

static inline int
fpe_fill(shmemio_client_fpe_t *fpe, shmemio_fspace_t *fio)
{
  ucs_status_t s;
  ucp_ep_params_t epm;

  epm.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
  epm.address = fpe->server_addr;

  s = ucp_ep_create(fio->ch->w, &epm, &fpe->server_ep);

  if (s != UCS_OK) {
    shmemio_log(error, "Failed to create remote endpoint on file server\n");
    goto err;
  }

  shmemio_log(trace, "Fill in fpe endpoint %p\n", fpe->server_ep);
  return 0;
  
  //err_ep:
  //fpe_ep_destroy(fpe, fio);
  
 err:
  fpe->server_ep = NULL;
  return -1;
}

static inline void
fpe_range_reset(int start, int end, int fid)
{
  for (int idx = start; idx < end; idx++) {
    shmemio_client_fpe_t *fpe = &(proc.io.fpes[idx]);
    fpe->valid = 0;
    fpe->fspace = fid;

    fpe->server_addr = NULL;
    fpe->server_ep = NULL;
  }
}

static inline int
fpe_recv_init(shmemio_client_fpe_t *fpe, size_t addr_len)
{
  fpe->server_addr_len = addr_len;

  fpe->server_addr = malloc(fpe->server_addr_len);
  if (fpe->server_addr == NULL) {
    return -1;
  }
  return 0;
}


static inline void
fspace_release_fpes(shmemio_fspace_t *fio)
{
  for (int idx = fio->fpe_start; idx < fio->fpe_end; idx++) {
    shmemio_client_fpe_t *fpe = &(proc.io.fpes[idx]);

    if (fpe->server_ep != NULL) {
      fpe_ep_destroy(fpe, fio);
    }
    
    if (fpe->server_addr != NULL)
      free (fpe->server_addr);

    proc.comms.eps[fpe_to_pe_index(idx)] = NULL;
  }
  
  //TODO: reclaim memory, maybe
  fpe_range_reset(fio->fpe_start, fio->fpe_end, -1);
}


static inline int
fpe_range_find_free(int size, int *fstart, int *fend)
{
  int fs = 0;
  int fe = 0;
  
  for (int idx = 0; idx < proc.io.nfpes; idx++) {
    if (proc.io.fpes[idx].valid == 1) {
      fs = idx;
      fe = idx;
    }
    else {
      fe = idx;
    }
    if ((fe - fs) >= size) {
      *fend = fe;
      *fstart = fs;
      return (fe - fs);
    }
  }

  return 0;
}

static inline void
fpe_extend_proc_comms_eps(int newcount)
{
  ucp_ep_h *new_eps =
    (ucp_ep_h*) realloc(proc.comms.eps, newcount * sizeof(ucp_ep_h));

  shmemio_assert(new_eps != NULL, "realloc error\n");

  proc.comms.eps = new_eps;
}

static inline int
fpe_range_assign(shmemio_fspace_t *fio, int fid)
{
  int nfpes = fio->nfpes;

  if (proc.io.nfpes == 0) {
    proc.io.fpes = (shmemio_client_fpe_t*)malloc(nfpes * sizeof(shmemio_client_fpe_t));
    fio->fpe_start = 0;
    fio->fpe_end = nfpes;

    proc.io.nfpes = nfpes;
    fpe_extend_proc_comms_eps(total_n_pes());

    fpe_range_reset(fio->fpe_start, fio->fpe_end, fid);
    return nfpes;
  }

  int fs, fe;

  if (fpe_range_find_free(nfpes, &fs, &fe) == nfpes) {
    fio->fpe_start = fs;
    fio->fpe_end = fe;
    fpe_range_reset(fio->fpe_start, fio->fpe_end, fid);
    return nfpes;
  }

  //TODO: alloc more space

 err_init:
  fpe_range_reset(fio->fpe_start, fio->fpe_end, -1);
  fio->fpe_start = -1;
  fio->fpe_end = -1;
  
 err:
  return -1;
}

#endif
