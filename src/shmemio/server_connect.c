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
shmemio_fill_stats(shmemio_sfile_t *sfile, shmemio_fp_stat_t* fstat)
{
  fstat->size = sfile->size;
  memcpy(&fstat->ctime, &sfile->ctime, sizeof(time_t));
  memcpy(&fstat->atime, &sfile->atime, sizeof(time_t));
  memcpy(&fstat->mtime, &sfile->mtime, sizeof(time_t));
  memcpy(&fstat->ftime, &sfile->ftime, sizeof(time_t));
}

static inline shmemio_conn_t*
shmemio_create_new_conn(shmemio_server_t *srvr, ucp_ep_h ep)
{
  shmemio_conn_t* newconn = (shmemio_conn_t*)malloc(sizeof(shmemio_conn_t));
  shmemio_assert((newconn != NULL), "failed to allocate connection object\n");

  //number sfpes never changes... for now...
  newconn->nfpes = srvr->nsfpes;

  //TODO: change to = 1 for lazy update...
  newconn->nregions = srvr->nregions;

  newconn->acked = 0;
  newconn->flags = 0;

  newconn->ep = ep;

  newconn->open_sfiles = NULL;
  
  shmemio_mutex_lock(&(srvr->req_conn_ls_lock));
  if (srvr->req_conns != NULL) {
    srvr->req_conns->prev = newconn;
  }
  newconn->next = srvr->req_conns;
  newconn->prev = NULL;
  srvr->req_conns = newconn;
  shmemio_mutex_unlock(&(srvr->req_conn_ls_lock));
  
  return newconn;
}

static inline shmemio_conn_t*
shmemio_pop_new_conn(shmemio_server_t *srvr)
{
  shmemio_conn_t *ret = NULL;
  
  shmemio_mutex_lock(&(srvr->req_conn_ls_lock));
  if (srvr->req_conns == NULL) {
    goto unlock_return_conn;
  }
  
  ret = (shmemio_conn_t*)srvr->req_conns;
  srvr->req_conns = ret->next;

  if (srvr->req_conns != NULL) {
    srvr->req_conns->prev = NULL;
  }

  ret->prev = NULL;
  ret->next = NULL;
  
 unlock_return_conn:
  shmemio_mutex_unlock(&(srvr->req_conn_ls_lock));
  return ret;
}

static inline int
shmemio_kill_connection(shmemio_server_t *srvr, shmemio_conn_t *conn)
{
  shmemio_ep_force_close(srvr->worker, conn->ep);
  conn->ep = NULL;
}

static inline void
shmemio_release_new_conn(shmemio_server_t *srvr, shmemio_conn_t *conn)
{
  shmemio_assert((conn->prev == NULL) && (conn->next == NULL), "release new connection not popped from list\n");

  if (conn->open_sfiles != NULL) {
    shmemio_log(warn, "Open files for new connection that never acknowledged. Should not occur.\n");
    shmemio_conn_close_all_files(srvr, conn);
  }
  if (conn->ep != NULL) {
    shmemio_kill_connection(srvr, conn);
  }
  
  free(conn);
}

static inline void
shmemio_release_client_conn(shmemio_server_t *srvr, shmemio_conn_t *in_conn)
{
  shmemio_mutex_lock(&(srvr->cli_conn_ls_lock));
  shmemio_conn_t* conn = (shmemio_conn_t*)((in_conn == NULL) ? srvr->cli_conns : in_conn);

  shmemio_log(info, "Release connection %p, ep %p\n", conn, conn->ep); 

  if (conn == NULL) {
    return;
  }
  if (srvr->cli_conns == conn) {
    srvr->cli_conns = conn->next;
  }
  if (conn->next != NULL) {
    conn->next->prev = conn->prev;
  }
  if (conn->prev != NULL) {
    conn->prev->next = conn->next;
  }

  khint_t k;
  k = kh_get(ptr2ptr, srvr->cli_conn_hash, (uint64_t)conn->ep);
  if (k == kh_end(srvr->cli_conn_hash)) {
    shmemio_log(warn, "Could not find ep %p in hash? Connections should be initialized with valid ep\n");
  }
  else {
    kh_del(ptr2ptr, srvr->cli_conn_hash, k);
  }

  shmemio_mutex_unlock(&(srvr->cli_conn_ls_lock));

  shmemio_log_if(warn, !conn->acked, "releasing connection that never got ack back? Only add client connections using shmemio_client_ack_connect\n");

  if (conn->open_sfiles != NULL) {
    shmemio_conn_close_all_files(srvr, conn);
  }
  if (conn->ep != NULL) {
    shmemio_kill_connection(srvr, conn);
  }

  free(conn);
}

shmemio_conn_t*
shmemio_ep_to_conn(shmemio_server_t* srvr, ucp_ep_h ep)
{
  shmemio_conn_t *ret = NULL;
  
  shmemio_mutex_lock(&(srvr->cli_conn_ls_lock));
  
  khint_t k = kh_get(ptr2ptr, srvr->cli_conn_hash, (uint64_t)ep);
  if (k != kh_end(srvr->cli_conn_hash))
    ret = kh_val(srvr->cli_conn_hash, k);

  shmemio_mutex_unlock(&(srvr->cli_conn_ls_lock));

  return ret;
}

static inline int
shmemio_client_ack_connect(shmemio_server_t *srvr, shmemio_conn_t *newconn, shmemio_connreq_t* req)
{
  newconn->acked = ( (req->nfpes > 0) && (req->nfpes <= srvr->nsfpes) &&
		     (req->nregions > 0) && (req->nregions <= srvr->nregions) );

  if (!newconn->acked) {
    return -1;
  }

  newconn->prev = NULL;
  
  shmemio_mutex_lock(&(srvr->cli_conn_ls_lock));
  newconn->next = srvr->cli_conns;
  srvr->cli_conns = newconn;

  if (newconn->next != NULL) {
    newconn->next->prev = newconn;
  }

  int absent;
  khint_t k = kh_put(ptr2ptr, srvr->cli_conn_hash, (uint64_t)newconn->ep, &absent);
  shmemio_log_jmp_if(error, err_unlock, !absent,
		     "Ep %p already hashed to client connection\n", newconn->ep);

  kh_val(srvr->cli_conn_hash, k) = newconn;

  shmemio_mutex_unlock(&(srvr->cli_conn_ls_lock));
  return 0;

 err_unlock:
  shmemio_mutex_unlock(&(srvr->cli_conn_ls_lock));
  return -1;
}

/**
 * Error handling callback.
 */
static void err_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    printf("error handling callback was invoked with status %d (%s)\n",
           status, ucs_status_string(status));
}


/**
 * The callback on the server side which is invoked upon receiving a connection
 * request from the client.
 */
static void
//server_accept_cb(ucp_ep_h ep, void *arg)
server_conn_handle_cb(ucp_conn_request_h conn_request, void *arg)
{
    shmemio_server_t *srvr = arg;
    ucp_ep_h         ep;
    ucp_ep_params_t  ep_params;
    ucs_status_t     status;
    ucp_worker_h     worker = srvr->worker;

    /* Server creates an ep to the client on the worker.
     * TODO: Add a data worker separate from the connection listener
     * This would not be the worker the listener was created on.
     * The client side should have initiated the connection, leading
     * to this ep's creation */
    ep_params.field_mask      = UCP_EP_PARAM_FIELD_ERR_HANDLER |
                                UCP_EP_PARAM_FIELD_CONN_REQUEST;
    ep_params.conn_request    = conn_request;
    ep_params.err_handler.cb  = err_cb;
    ep_params.err_handler.arg = NULL;

    status = ucp_ep_create(worker, &ep_params, &ep);
    if (status != UCS_OK) {
        fprintf(stderr, "failed to create an endpoint on the server: (%s)\n",
                ucs_status_string(status));
        return;
    }

    /* Save the client endpoint for future usage */
    shmemio_create_new_conn(srvr, ep);
}

int
shmemio_listen(shmemio_server_t *srvr)
{
  struct sockaddr_in listen_addr;
  ucp_listener_params_t params;
  ucs_status_t status;

  /* The server will listen on INADDR_ANY */
  memset(&listen_addr, 0, sizeof(struct sockaddr_in));
  listen_addr.sin_family      = AF_INET;
  listen_addr.sin_addr.s_addr = INADDR_ANY;
  listen_addr.sin_port        = srvr->port;

  params.field_mask         = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
                              UCP_LISTENER_PARAM_FIELD_CONN_HANDLER;    
    //UCP_LISTENER_PARAM_FIELD_ACCEPT_HANDLER;
  params.sockaddr.addr      = (const struct sockaddr*)&listen_addr;
  params.sockaddr.addrlen   = sizeof(listen_addr);
  //params.accept_handler.cb  = server_accept_cb;
  //params.accept_handler.arg = srvr;
  params.conn_handler.cb    = server_conn_handle_cb;
  params.conn_handler.arg   = srvr;  

  /* Create a listener on the server side to listen on the given address.*/
  status = ucp_listener_create(srvr->worker, &params, &(srvr->listener));
  if (status != UCS_OK) {
    shmemio_log(error, "failed to create listener (%s)\n",
		ucs_status_string(status));
    return -1;
  }

  srvr->status = shmemio_server_listen;
  return 0;
}

static inline int
shmemio_send_sfpe_mem(ucp_worker_h worker, ucp_ep_h ep,
				 shmemio_sfpe_mem_t *sm)
{
  int ret;
  size_t rbuf[4];
  const size_t rbuf_size = sizeof(size_t) * 4;
  //remote_region_recv_init(rreg, rbuf[0], rbuf[1], rbuf[2], rbuf[3]); -> base, end, len, rkey_len

  rbuf[0] = sm->base;
  rbuf[1] = sm->end;
  rbuf[2] = sm->len;
  rbuf[3] = sm->rkey_len;

  ret = shmemio_streamsend(worker, ep, rbuf, rbuf_size);
  shmemio_log_ret_if(error, -1, (ret < 0), "failed to send sfpe mem size_t fields");

  ret = shmemio_streamsend(worker, ep, sm->packed_rkey, sm->rkey_len);
  shmemio_log_ret_if(error, -1, (ret < 0), "failed to send sfpe mem rkey\n");

  return 0;
}


static inline int
shmemio_send_region(ucp_worker_h worker, ucp_ep_h ep, shmemio_server_region_t *reg)
{
  int ret;
  int ibuf[4];
  size_t ibufsize = sizeof(int) * 4;
  //client_region_recv_init(reg, ibuf[0], ibuf[1], ibuf[2], ibuf[3]); -> fpe_start, fpe_stride, fpe_size, unit_size

  ibuf[0] = reg->sfpe_start;
  ibuf[1] = reg->sfpe_stride;
  ibuf[2] = reg->sfpe_size;
  ibuf[3] = reg->unit_size;
  
  ret = shmemio_streamsend(worker, ep, ibuf, ibufsize);
  shmemio_log_ret_if(error, -1, (ret < 0), "failed to send sfpe int fields\n");

  for (int idx = 0; idx < reg->sfpe_size; idx++) {
    shmemio_log(trace, "sending sfpe memory keys for sfpe %d of %d\n",
		idx, reg->sfpe_size);
    ret = shmemio_send_sfpe_mem(worker, ep, &(reg->sfpe_mems[idx]));
    shmemio_log_ret_if(error, -1, (ret < 0), "failed to send sfpe mem info\n");
  }

  return 0;
  
 err:
  return -1;
}

static inline int
shmemio_send_sfpe(ucp_worker_h worker, ucp_ep_h ep, shmemio_server_fpe_t *sfpe)
{
  int ret;

  ret = shmemio_streamsend(worker, ep, &(sfpe->worker_addr_len), sizeof(sfpe->worker_addr_len));
  shmemio_log_ret_if(error, -1, (ret < 0), "failed to send sfpe worker addr\n");
  
  ret = shmemio_streamsend(worker, ep, sfpe->worker_addr, sfpe->worker_addr_len);
  shmemio_log_ret_if(error, -1, (ret < 0), "failed to send sfpe worker addr\n");

  return 0;
}

static inline int
shmemio_send_regions(shmemio_server_t *srvr, ucp_ep_h ep, int rstart, int rmax)
{
  int ret;
  shmemio_log(info, "Sending regions %d:%d...\n", rstart, rmax-1);

  for (int idx = rstart; idx < rmax; idx++) {
    shmemio_log(trace, "Send region %d:%d\n", idx, rmax-1);
    
    ret = shmemio_send_region(srvr->worker, ep, &(srvr->regions[idx]));
    shmemio_log_ret_if(error, -1, (ret < 0), "failed to send region\n");
  }

  return 0;
}

static inline int
shmemio_send_fspace(shmemio_server_t *srvr, shmemio_conn_t *conn)
{
  int ret;
  shmemio_log(info, "Sending %d fpes...\n", conn->nfpes);
  
  for (int idx = 0; idx < conn->nfpes; idx++) {
    shmemio_log(trace, "Send fpe %d of %d\n", idx, conn->nfpes);
    
    ret = shmemio_send_sfpe(srvr->worker, conn->ep, &(srvr->sfpes[idx]));
    shmemio_log_ret_if(error, -1, (ret < 0), "failed to send sfpe\n");
  }

  return shmemio_send_regions(srvr, conn->ep, 0, conn->nregions);
}

int
shmemio_send_response(shmemio_server_t *srvr, ucp_ep_h ep, shmemio_req_t *req, int status)
{
  req->status = status;
  shmemio_log(trace, "Send response to ep %p for req type %d with status %d\n", ep, req->type, req->status);
  int ret = shmemio_streamsend(srvr->worker, ep, req, sizeof(shmemio_req_t));
  shmemio_log_if(error, ret < 0, "Failed to send server response\n");
  return ret;
}

static inline void
shmemio_check_fkey_ep(uint64_t fkey, ucp_ep_h ep)
{
  shmemio_sfile_ls_t *snode = (shmemio_sfile_ls_t *)fkey;

  shmemio_assert(snode->conn->ep == ep,
		 "fkey encodes ep %p but request from ep %p",
		 snode->conn->ep, ep);
}

static inline void
shmemio_fspace_stat(shmemio_server_t *srvr, shmem_fspace_stat_t *fsstat)
{
  fsstat->pe_start = 0;
  fsstat->pe_size = srvr->nsfpes;

  fsstat->nregions = srvr->nregions;
  fsstat->nfiles = srvr->nfiles;

  size_t bused = 0;
  size_t bfree = 0;
  mallinfo_t mi;
  for (int idx = 0; idx < srvr->nregions; idx++) {
    shmemio_region_mallinfo(&mi, &(srvr->regions[idx]));
    bused += mi.uordblks + mi.hblkhd;
    bfree += mi.fordblks;
  }
  fsstat->used_size = bused;
  fsstat->free_size = bfree;
}

static inline int
shmemio_handle_request(shmemio_server_t *srvr, ucp_ep_h ep)
{
  //shmemio_log(trace, "Handle request from ep %p\n", ep);
  
  int ret;
  shmemio_req_t req;
  ret = shmemio_streamrecv(srvr->worker, ep, &req, sizeof(shmemio_req_t));
  shmemio_log_ret_if(error, -1, ret < 0, "fail on recv new shmemio server request\n");
  
  shmemio_log(trace, "Got new request type %d [%s] from shmemio client at ep %p...\n",
	      req.type, shmemio_rt2str(req.type), ep);

  switch(req.type) {
  case shmemio_fopen_req:
    {
      shmemio_server_fopen(srvr, ep, (shmemio_fopen_req_t*)req.payload, &(req.status));
      return shmemio_send_response(srvr, ep, &req, req.status);
    }
  case shmemio_region_req:
    {
      ret = shmemio_send_regions(srvr, ep, ((int*)req.payload)[0], ((int*)req.payload)[1]);
      shmemio_log_ret_if(error, -1, ret < 0, "Failed to send regions\n");
      return ret;
    }
  case shmemio_disco_req:
    {
      shmemio_conn_t *conn = shmemio_ep_to_conn(srvr, ep);
      if (conn == NULL) {
	shmemio_log(warn, "Got disco request from ep %p not mapped to any known connection\n", ep);
	return -1;
      }

      shmemio_release_client_conn(srvr, conn);
      return 0;
    }
  case shmemio_fp_flush_req:
  case shmemio_fclose_req:
  case shmemio_ftrunc_req:
    {
      shmemio_fp_req_t *fpreq = get_fpreq(&req);
      shmemio_do_error(shmemio_check_fkey_ep(fpreq->fkey, ep));
      
      shmemio_try_blocking_file_act(srvr, req.type,
				    fpreq, &(req.status));

      if (req.status != shmemio_action_blocked) {
	//action did not block
	return shmemio_send_response(srvr, ep, &req, req.status);
      }
      //action blocked. No response until complete.
      return 0;
    }
  case shmemio_fextend_req:
    {
      shmemio_fp_req_t *fpreq = get_fpreq(&req);
      shmemio_do_error(shmemio_check_fkey_ep(fpreq->fkey, ep));
      
      shmemio_try_nonblock_file_act(srvr, req.type,
				    fpreq, &(req.status));
      
      return shmemio_send_response(srvr, ep, &req, req.status);
    }
  case shmemio_fp_stat_req:
    {
      shmemio_fp_req_t *fpreq = get_fpreq(&req);
      shmemio_do_error(shmemio_check_fkey_ep(fpreq->fkey, ep));

      shmemio_fp_stat_t fpstat;
      shmemio_sfile_t *sfile = ((shmemio_sfile_ls_t*)fpreq->fkey)->sfile;
      shmemio_fill_stats(sfile, &fpstat);
      shmemio_log_sfile(info, *sfile, "respond to fstat");
      shmemio_streamsend(srvr->worker, ep, &fpstat, sizeof(shmemio_fp_stat_t));
      return 0;
    }
  case shmemio_fspace_flush_req:
    {
      shmemio_flush_fspace(srvr, 0);
      return shmemio_send_response(srvr, ep, &req, shmemio_success);
    }
  case shmemio_fspace_stat_req:
    {
      shmem_fspace_stat_t fsstat;
      shmemio_fspace_stat(srvr, &fsstat);
      shmemio_streamsend(srvr->worker, ep, &fsstat, sizeof(shmem_fspace_stat_t));
      return 0;
    }
  default:
    shmemio_log(error, "Unhandled type %d recv by server\n", req.type);
    return -1;
  }

  return 0;
}


static inline int
shmemio_handshake(shmemio_server_t *srvr, shmemio_conn_t *newconn)
{
  int ret;
  shmemio_connreq_t req;
  req.nfpes = newconn->nfpes;
  req.nregions = newconn->nregions;

  // Removing this causes an error in first stream send from server->client?
  test_send_recv_stream(srvr->worker, newconn->ep, 1);
  
  shmemio_log(info,
	      "Start handshake on ep %p with nsfes %d and nregions %d...\n",
	      newconn->ep, newconn->nfpes, newconn->nregions);
  
  ret = shmemio_streamsend(srvr->worker, newconn->ep, &req, sizeof(shmemio_connreq_t));
  shmemio_log_jmp_if(error, err_conn, ret < 0, "send connection info\n");
  
  ret = shmemio_streamrecv(srvr->worker, newconn->ep, &req, sizeof(shmemio_connreq_t));
  shmemio_log_jmp_if(error, err_conn,
		     (ret != sizeof(shmemio_connreq_t)),
		     "recv connections ack from client\n");
  
  ret = shmemio_client_ack_connect(srvr, newconn, &req);
  shmemio_log_jmp_if(error, err_conn,
		     (ret < 0), "new connection rejected by client\n");

  return 0;

 err_conn:
  /* Close the endpoint to the client on error */
  shmemio_release_new_conn(srvr, newconn);

  return -1;
}

int
shmemio_connectloop(shmemio_server_t *srvr)
{
  int ret;
  size_t len;
  static const size_t max_eps = 10;
  ucp_stream_poll_ep_t poll_eps[max_eps];
  
  while (srvr->status == shmemio_server_listen) {
    wait_next_conn:
    ;
    shmemio_log(trace, "Waiting for connection...\n");
    
    while (srvr->req_conns == NULL) {
      while (ucp_worker_progress(srvr->worker) != 0) {
	//shmemio_log(trace, "Progress server worker\n");
      }
      
      if (srvr->status != shmemio_server_listen)
	goto killall_newconns;

      //shmemio_log(trace, "Polling server worker\n");
      ssize_t count = ucp_stream_worker_poll(srvr->worker, poll_eps, max_eps, 0);
      
      while (count > 0) {
	shmemio_log(trace, "Received requests from %d eps\n", count);
      
	for (int idx = 0; idx < count; idx++) {
	  shmemio_handle_request(srvr, poll_eps[idx].ep);
	}
	while (ucp_worker_progress(srvr->worker) != 0);
	
	count = ucp_stream_worker_poll(srvr->worker, poll_eps, max_eps, 0);
      }

      //Currently busted on bluefield, never returns, emulated atomics problem?
      //shmemio_log(trace, "Enter server worker wait\n");
      //ucs_status_t status = ucp_worker_wait(srvr->worker);
      //shmemio_log_if(warn, status != UCS_OK, "ucp_worker_wait returned error\n");
    }

    shmemio_log(trace, "Got connection...\n");
    shmemio_conn_t *newconn = shmemio_pop_new_conn(srvr);
    if (newconn == NULL) {
      goto wait_next_conn;
    }

    ret = shmemio_handshake(srvr, newconn);
    shmemio_log_jmp_if(error, wait_next_conn, ret != 0, "New connection handshake failed\n");

    ret = shmemio_send_fspace(srvr, newconn);
    shmemio_log_jmp_if(error, wait_next_conn, ret != 0, "Send fspace failed\n");
      
    (*(srvr->conn_cb_f))(srvr, newconn, srvr->conn_cb_args);
    
  } // end while server is running
  
 killall_newconns:
  ;
  shmemio_conn_t *newconn = shmemio_pop_new_conn(srvr);
  while(newconn != NULL) {
    shmemio_release_new_conn(srvr, newconn);
    newconn = shmemio_pop_new_conn(srvr);
  }

  return 0;
}


int
shmemio_release_all_conns(shmemio_server_t *srvr) {

  shmemio_conn_t *newconn = shmemio_pop_new_conn(srvr);
  while(newconn != NULL) {
    shmemio_release_new_conn(srvr, newconn);
    newconn = shmemio_pop_new_conn(srvr);
  }
  
  while (srvr->cli_conns != NULL) {
    shmemio_release_client_conn(srvr, NULL);
  }
}

