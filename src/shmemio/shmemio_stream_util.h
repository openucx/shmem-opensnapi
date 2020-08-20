/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef  SHMEMIO_STREAM_UTIL_H
#define  SHMEMIO_STREAM_UTIL_H

#include <unistd.h>

/**
 * Close the given endpoint.
 * Currently closing the endpoint with UCP_EP_CLOSE_MODE_FORCE since we currently
 * cannot rely on the client side to be present during the server's endpoint
 * closing process.
 */
static void
shmemio_ep_force_close(ucp_worker_h ucp_worker, ucp_ep_h ep)
{
    ucs_status_t status;
    void *close_req;

    close_req = ucp_ep_close_nb(ep, UCP_EP_CLOSE_MODE_FORCE);
    if (UCS_PTR_IS_PTR(close_req)) {
      do {
	ucp_worker_progress(ucp_worker);
	
	status = ucp_request_check_status(close_req);
      } while (status == UCS_INPROGRESS);
      
      ucp_request_free(close_req);
    } else if (UCS_PTR_STATUS(close_req) != UCS_OK) {
      shmemio_log(warn, "failed to close ep %p\n", (void*)ep);
    }
}

static size_t
shmemio_request_wait(ucp_worker_h ucp_worker, shmemio_streamreq_t *request)
{
    /*  if operation was completed immediately */
    if (request == NULL) {
        return UCS_OK;
    }

    if (UCS_PTR_IS_ERR(request)) {
        return UCS_PTR_STATUS(request);
    }
    
    while (request->complete == 0) {
      //shmemio_log(trace, "Worker progress spin\n");
      //sleep(1);
      ucp_worker_progress(ucp_worker);
    }

  shmemio_log(trace, "done with wait: request = %p, complete-1 = %d\n", request, request->complete - 1);
  
  size_t ret = request->complete - 1;
  /* This request may be reused so initialize it for next time */
  request->complete = 0;
  ucp_request_free(request);
  
  return ret;
}

/**
 * The callback on the sending side, which is invoked after finishing sending
 * the stream message.
 */
static void
stream_send_cb(void *request, ucs_status_t status)
{
    shmemio_streamreq_t *req = request;
    req->complete = 1;

    shmemio_log(trace,
		"stream_send_cb req %p, returned with status %d (%s)\n",
    		req, status, ucs_status_string(status), req);
}

static inline int
shmemio_streamsend(ucp_worker_h worker, ucp_ep_h ep, const void *data, size_t len)
{
  shmemio_streamreq_t *request;

  shmemio_log(trace,
	      "ucp_stream_send_nb(ep=%p, data=%p, len=%u)\n",
	      ep, data, len);
  
  request = ucp_stream_send_nb(ep, data, 1,
			       ucp_dt_make_contig(len),
			       stream_send_cb, 0);
    
  if (UCS_PTR_IS_ERR(request)) {
    shmemio_log(error,
		"unable to send UCX message (%s)\n",
		ucs_status_string(UCS_PTR_STATUS(request)));
    return -1;
  } else if (UCS_PTR_STATUS(request) != UCS_OK) {
    shmemio_request_wait(worker, request);
  }

  shmemio_log(trace,
	      "stream send done(ep=%p, data=%p) done wait len = %lu\n", ep, data, len);

  return 0;
}

/**
 * The callback on the receiving side, which is invoked upon receiving the
 * stream message.
 */
static void
stream_recv_cb(void *request, ucs_status_t status, size_t length)
{
    shmemio_streamreq_t *req = request;
    req->complete = length + 1;

    shmemio_log(trace,
		"stream_recv_cb req %p, returned with status %d (%s), length: %lu\n",
		req, status, ucs_status_string(status), length);
}

static inline int
shmemio_streamrecv(ucp_worker_h worker, ucp_ep_h ep, void *data, size_t sz)
{
  size_t len = sz;
  shmemio_streamreq_t *request;
  
  shmemio_log(trace,
	      "ucp_stream_recv_nb(ep=%p, data=%p)\n", ep, data);

  request = ucp_stream_recv_nb(ep, data, 1,
			       ucp_dt_make_contig(sz),
			       stream_recv_cb, &len, 0);
    
  if (UCS_PTR_IS_ERR(request)) {
    shmemio_log(error,
		"unable to recv UCX message (%s)\n",
		ucs_status_string(UCS_PTR_STATUS(request)));
    return -1;
  } else if (UCS_PTR_STATUS(request) != UCS_OK) {
    len = shmemio_request_wait(worker, request);
  }

  shmemio_log(trace,
	      "stream recv done(ep=%p, data=%p) done wait len = %lu\n", ep, data, len);
  
  return (int)len;
}


/**
 * Send and receive a message using the Stream API.
 * The client sends a message to the server and waits until the send it completed.
 * The server receives a message from the client and waits for its completion.
 */
typedef struct test_req {
    int complete;
} test_req_t;

static ucs_status_t test_request_wait(ucp_worker_h ucp_worker, test_req_t *request)
{
    ucs_status_t status;

    /*  if operation was completed immediately */
    if (request == NULL) {
        return UCS_OK;
    }
    
    if (UCS_PTR_IS_ERR(request)) {
        return UCS_PTR_STATUS(request);
    }
    
    while (request->complete == 0) {
        ucp_worker_progress(ucp_worker);
    }
    status = ucp_request_check_status(request);

    /* This request may be reused so initialize it for next time */
    request->complete = 0;
    ucp_request_free(request);

    return status;
}

static void print_result(int is_server, char *recv_message)
{
    if (is_server) {
        printf("UCX data message was received\n");
        printf("\n\n----- UCP TEST SUCCESS -------\n\n");
        printf("%s", recv_message);
        printf("\n\n------------------------------\n\n");
    } else {
        printf("\n\n-----------------------------------------\n\n");
	printf("Client sent message\n");
        printf("\n-----------------------------------------\n\n");
    }
}

// TODO: currently client/server connection does not work if
// this function is not run before calling the handshake routines
// Perhaps because this starts with client send to server
// but handshake starts with server send to client first?
// Need more testing.
static int test_send_recv_stream(ucp_worker_h ucp_worker,
				 ucp_ep_h ep, int is_server)
{
  const char test_message[]   = "UCX Client-Server Hello World";
  char recv_message[sizeof(test_message)]= "";
  const size_t TEST_STRING_LEN = sizeof(test_message);
  
  test_req_t *request;
  size_t length;
  int ret = 0;
  ucs_status_t status;

  if (!is_server) {
    /* Client sends a message to the server using the stream API */
    request = ucp_stream_send_nb(ep, test_message, 1,
				 ucp_dt_make_contig(TEST_STRING_LEN),
				 stream_send_cb, 0);
  } else {
    /* Server receives a message from the client using the stream API */
    request = ucp_stream_recv_nb(ep, &recv_message, 1,
				 ucp_dt_make_contig(TEST_STRING_LEN),
				 stream_recv_cb, &length,
				 UCP_STREAM_RECV_FLAG_WAITALL);
  }
  
  status = test_request_wait(ucp_worker, request);
  if (status != UCS_OK){
    fprintf(stderr, "unable to %s UCX message (%s)\n",
	    is_server ? "receive": "send",
	    ucs_status_string(status));
    ret = -1;
  }
#if 0
  else {
    if (is_server) {
        printf("UCX data message was received\n");
        printf("\n\n----- UCP TEST SUCCESS -------\n\n");
        printf("%s", recv_message);
        printf("\n\n------------------------------\n\n");
    } else {
        printf("\n\n-----------------------------------------\n\n");
	printf("Client sent message\n");
        printf("\n-----------------------------------------\n\n");
    }
#endif
  
  return ret;
}


#endif /* SHMEMIO_STREAM_UTIL_H */
