/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#include "shmemu.h"
#include "shmemc.h"
#include "shmem.h"

#include "shmemio.h"
#include "shmemio_client.h"

#include "shmemio_test_util.h"

#include "shmemio_client_fpe.h"
#include "shmemio_client_region.h"
#include "shmemio_stream_util.h"

/**
 * Error handling callback.
 */
static void err_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    printf("error handling callback was invoked with status %d (%s)\n",
           status, ucs_status_string(status));
}

/**
 * Initialize the client side. Create an endpoint from the client side to be
 * connected to the remote server (to the given IP).
 */
static int
shmemio_connect( ucp_worker_h worker, const char *server_name, uint16_t server_port, ucp_ep_h *ep)
{
  ucp_ep_params_t ep_params;
  struct sockaddr_in connect_addr;
  ucs_status_t status;

  memset(&connect_addr, 0, sizeof(struct sockaddr_in));
  connect_addr.sin_family      = AF_INET;
  connect_addr.sin_addr.s_addr = inet_addr(server_name);
  connect_addr.sin_port        = server_port;

  /*
   * Endpoint field mask bits:
   * UCP_EP_PARAM_FIELD_FLAGS             - Use the value of the 'flags' field.
   * UCP_EP_PARAM_FIELD_SOCK_ADDR         - Use a remote sockaddr to connect
   *                                        to the remote peer.
   * UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE - Error handling mode - this flag
   *                                        is temporarily required since the
   *                                        endpoint will be closed with
   *                                        UCP_EP_CLOSE_MODE_FORCE which
   *                                        requires this mode.
   *                                        Once UCP_EP_CLOSE_MODE_FORCE is
   *                                        removed, the error handling mode
   *                                        will be removed.
   */
  ep_params.field_mask       = ( UCP_EP_PARAM_FIELD_FLAGS     |
				 UCP_EP_PARAM_FIELD_SOCK_ADDR |
				 UCP_EP_PARAM_FIELD_ERR_HANDLER );// |
  //UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE );
  //ep_params.err_mode         = UCP_ERR_HANDLING_MODE_PEER;
  ep_params.err_handler.cb   = err_cb;
  ep_params.err_handler.arg  = NULL;
  ep_params.flags            = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
  ep_params.sockaddr.addr    = (struct sockaddr*)&connect_addr;
  ep_params.sockaddr.addrlen = sizeof(connect_addr);
  
  status = ucp_ep_create(worker, &ep_params, ep);

  shmemio_log(info,
	      "connect: ucp_ep_create returned with status %d (%s), ep: %p\n",
	      status, ucs_status_string(status), *ep);
  
  if (status != UCS_OK) {
    shmemio_log(error,
		"failed to connect to %s:%u (%s)\n",
		server_name, (unsigned)server_port, ucs_status_string(status));
    return -1;
  }

  return 0;
}

static inline int
shmemio_recv_r_region(ucp_worker_h worker, ucp_ep_h ep, shmemio_remote_region_t *rreg)
{
  size_t rbuf[4];
  const size_t rbuf_size = sizeof(size_t) * 4;
  int ret;

  ret = shmemio_streamrecv(worker, ep, rbuf, rbuf_size);
  shmemio_log_ret_if(error, -1, (ret != rbuf_size), "failed to recv remote region size fields\n");

  ret = remote_region_recv_init(rreg, rbuf[0], rbuf[1], rbuf[2], rbuf[3]);
  shmemio_log_ret_if(error, -1, (ret != 0), "remote region recv init fail\n");
  
  ret = shmemio_streamrecv(worker, ep, rreg->packed_rkey, rreg->rkey_len);
  shmemio_log_ret_if(error, -1, (ret != rreg->rkey_len), "failed in recv sfpe rkey\n");

  return 0;
}

static inline int
shmemio_recv_region(ucp_worker_h worker, ucp_ep_h ep, shmemio_client_region_t *reg)
{
  int ibuf[4];
  size_t ibuf_size = sizeof(int) * 4;
  int ret;

  ret = shmemio_streamrecv(worker, ep, ibuf, ibuf_size);
  shmemio_log_ret_if(error, -1, (ret != ibuf_size), "failed to recv region value fields\n");

  ret = client_region_recv_init(reg, ibuf[0], ibuf[1], ibuf[2], ibuf[3]);
  shmemio_log_ret_if(error, -1, (ret != 0), "client region recv init fail\n");
  
  for (int idx = 0; idx < reg->fpe_size; idx++) {
    ret = shmemio_recv_r_region(worker, ep, &(reg->r_regions[idx]));
    shmemio_log_ret_if(error, -1, (ret != 0), "failed to recv remote region info\n");

    const size_t newlen = reg->r_regions[idx].len;
    if (idx == 0)
      reg->len = newlen;

    if (newlen != reg->len) {
      shmemio_log(error, "Got region of remote memory len %u != other remote region lens %u\n",
		  newlen, reg->len);
      return -1;
    }
  }

  return 0;
}
 
static inline int
shmemio_recv_fpe(ucp_worker_h worker, ucp_ep_h ep, shmemio_client_fpe_t *fpe)
{
  int ret;
  size_t server_addr_len;

  ret = shmemio_streamrecv(worker, ep, &server_addr_len, sizeof(server_addr_len));
  shmemio_log_ret_if(error, -1, ret != sizeof(server_addr_len), "recv fpe server_addr_len\n");

  ret = fpe_recv_init(fpe, server_addr_len);
  shmemio_log_ret_if(error, -1, (ret != 0), "fpe recv init fail\n");

  ret = shmemio_streamrecv(worker, ep, fpe->server_addr, fpe->server_addr_len);
  shmemio_log_ret_if(error, -1, (ret != fpe->server_addr_len), "recv fpe server worker addr\n");

  return 0;
}

static inline int
shmemio_recv_regions(shmemio_fspace_t *fio, int rstart, int rmax)
{
  int ret;
  shmemio_log(info, "Recv regions %d:%d\n", rstart, rmax - 1);
  
  for (int idx = rstart; idx < rmax; idx++) {
    shmemio_log(info, "Recv region %d:%d\n", idx, rmax - 1);
    
    ret = shmemio_recv_region(fio->ch->w, fio->req_ep, &(fio->l_regions[idx]));
    shmemio_log_ret_if(error, -1, ret < 0,
		       "Failed on recv of region %d:%d\n",
		       idx, rmax - 1);
  }

  return 0;
}

static inline int
shmemio_recv_fspace(shmemio_fspace_t *fio)
{
  int ret;
  shmemio_log(info, "Recv %d:%d fpes...\n", fio->fpe_start, fio->fpe_end);
  
  for (int idx = fio->fpe_start; idx < fio->fpe_end; idx++) {
    shmemio_log(trace, "Recv fpe %d of [%d:%d]\n", idx, fio->fpe_start, fio->fpe_end);
    ret = shmemio_recv_fpe(fio->ch->w, fio->req_ep, &(proc.io.fpes[idx]));

    shmemio_log_ret_if(error, -1, ret < 0,
		       "Failed on recv of fpe %d of [%d:%d]\n",
		       idx, fio->fpe_start, fio->fpe_end);
  }

  return shmemio_recv_regions(fio, 0, fio->nregions);
}

// Other half of shmemio_handshake
int
shmemio_connect_fspace(shmem_fspace_conx_t *conx, int fid)
{
  size_t len;
  int ret;

  shmemio_connreq_t connreq;
  
  shmemio_fspace_t* fio = &proc.io.fspaces[fid];
  
  ret = shmemio_connect(fio->ch->w, conx->storage_server_name, conx->storage_server_port, &(fio->req_ep));
  shmemio_log_jmp_if(error, err, (ret < 0), "failed in shmemio_connect\n");

  // Currently breaks if we remove this?
  test_send_recv_stream(fio->ch->w, fio->req_ep, 0);
  
  ret = shmemio_streamrecv(fio->ch->w, fio->req_ep, &connreq, sizeof(shmemio_connreq_t));
  shmemio_log_jmp_if(error, err, (ret != sizeof(shmemio_connreq_t)), "failed to recv connection request data\n");

  /* ALLOC NEW FPES */
  
  fio->nfpes = connreq.nfpes;

  if (fpe_range_assign(fio, fid) != fio->nfpes) {
    shmemio_log(error, "failed to assign fpe range to fspace\n");
    connreq.nfpes = -1;
    goto err_ackfail;
  }
  
  /* ALLOC NEW TRANSLATION REGIONS */

  if (fspace_extend_client_regions(fio, connreq.nregions) < 0) {
    shmemio_log(error, "failed to allocate new regions in fspace\n");
    connreq.nregions = -1;
    goto err_ackfail;
  }
  
  //ack the connection that we can (probably) accept the fpes and regions
  shmemio_streamsend(fio->ch->w, fio->req_ep, &connreq, sizeof(shmemio_connreq_t));

  return shmemio_recv_fspace(fio);

 err_ackfail:
  shmemio_streamsend(fio->ch->w, fio->req_ep, &connreq, sizeof(shmemio_connreq_t));
  
 err:
  return -1;
}

static inline int
shmemio_req_regions(shmemio_fspace_t *fio, int new_nregions)
{
  int nnew = new_nregions - fio->nregions;
  shmemio_assert(nnew > 0, "Cannot reduce number of regions from %d to %d\n", fio->nregions, new_nregions);

  int nold = fspace_extend_client_regions(fio, nnew);
  shmemio_log_ret_if(error, -1, nold < 0, "failed to allocate new regions in fspace\n");

  shmemio_req_t req;
  req.type = shmemio_region_req;
  ((int*)req.payload)[0] = nold;
  ((int*)req.payload)[1] = fio->nregions;

  int ret = shmemio_streamsend(fio->ch->w, fio->req_ep, &req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, -1, ret < 0, "Failed to send region request\n");
  
  ret = shmemio_recv_regions(fio, nold, fio->nregions);
  shmemio_log_ret_if(error, -1, ret < 0, "Failed to recv regions\n");

  ret = shmemio_fill_regions(fio, nold, fio->nregions);
  shmemio_log_ret_if(error, -1, ret < 0, "Failed to fill regions\n");

  return 0;
}

int
shmemio_client_fopen(shmemio_fspace_t *fio, const char *file, shmemio_fp_t *fp, int *err)
{
  int ret;
  shmemio_req_t req;
  shmemio_fopen_req_t *foreq = (shmemio_fopen_req_t*)req.payload;

  req.type = shmemio_fopen_req;

  foreq->fsize        = fp->size;
  foreq->sfpe_start   = ( (fp->pe_start < 0) ?
			  fp->pe_start :
			  fpe_to_sfpe_index(*fio, pe_to_fpe_index(fp->pe_start)) );
  foreq->sfpe_stride  = fp->pe_stride;
  foreq->sfpe_size    = fp->pe_size;

  foreq->unit_size = fp->unit_size;

  if (file == NULL) {
    foreq->file_path_len = 0;
  }
  else {
    foreq->file_path_len = strlen(file);
  }
  
  req.status = shmemio_err_unknown;

  shmemio_log(info, "Sending request to open file %s (len=%d), size %lu, unit_size %d on pe [%d +%d] by %d\n",
	      file, foreq->file_path_len, foreq->fsize, foreq->unit_size,
	      foreq->sfpe_start, foreq->sfpe_size, foreq->sfpe_stride);

  ret = shmemio_streamsend(fio->ch->w, fio->req_ep, &req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, -1, ret < 0, "Failed to send fopen request\n");

  if (foreq->file_path_len != 0) {
    ret = shmemio_streamsend(fio->ch->w, fio->req_ep, file, foreq->file_path_len);
    shmemio_log_ret_if(error, -1, ret < 0, "Failed to send file path\n");
  }
  
  ret = shmemio_streamrecv(fio->ch->w, fio->req_ep, &req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, -1, ret != sizeof(shmemio_req_t), "Failed to recv fopen response\n");

  if (req.status != shmemio_success) {
    shmemio_log(error, "Server failed to open file\n");
    shmemio_seterr(err, req.status);
    return -1;
  }
  
  if (foreq->l_region >= fio->nregions) {
    shmemio_log(info, "fopen results in region id %d, I only have up to %d. Requesting that region\n",
		foreq->l_region, fio->nregions - 1);
    ret = shmemio_req_regions(fio, foreq->l_region + 1);
    if ( ret < 0 ) {
      shmemio_log(error, "Failed to request required regions for fopen\n");
      shmemio_seterr(err, shmemio_err_region_req);
      return -1;
    }
  }

  fp->pe_start   = fpe_to_pe_index(sfpe_to_fpe_index(*fio, foreq->sfpe_start));
  fp->pe_stride  = foreq->sfpe_stride;
  fp->pe_size    = foreq->sfpe_size;
  fp->unit_size  = foreq->unit_size;
  fp->size       = foreq->fsize;
  
  fp->l_region   = foreq->l_region;
  fp->offset     = foreq->offset;
  fp->addr       = (void*)(fio->l_regions[fp->l_region].l_base + fp->offset);
  fp->fkey       = foreq->fkey;
  return 0;
}

void
update_fp_status(shmemio_req_t *req, shmemio_fp_req_t *fpreq, shmem_fp_t *infp)
{
  if (req->status == shmemio_success) {
    shmemio_fp_t *fp = (shmemio_fp_t*)infp;
    
    shmemio_fspace_t *fio = &(proc.io.fspaces[fp->fspace]);

    fp->size      = fpreq->size;
    fp->offset    = fpreq->offset;
    fp->addr      = (void*)(fio->l_regions[fp->l_region].l_base + fp->offset);
    fp->fkey      = fpreq->fkey;
  }
}
