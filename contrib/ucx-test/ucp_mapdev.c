/* -----------------------------------------------
 * 
 * Copyright (c) 2018 - 2020 Arm, Ltd 
 *
 * Server side:
 *
 *    ./ucp_mapdev -t TESTNAME
 *
 * Client side:
 *
 *    ./ucp_mapdev -a <server-ip> -t TESTNAME
 *
 * Notes:
 *
 *    - The server will listen to incoming connection requests on INADDR_ANY.
 *    - The client needs to pass the IP address of the server side to connect to
 *      as an argument to the test.
 *    - Currently, the passed IP needs to be an IPoIB or a RoCE address.
 *    - The port which the server side would listen on can be modified with the
 *      '-p' option and should be used on both sides. The default port to use is
 *      13337.
 */

#include <ucp/api/ucp.h>
//#include <ucs/config/global_opts.h>

#include <string.h>    /* memset */
#include <arpa/inet.h> /* inet_addr */
#include <unistd.h>    /* getopt */
#include <stdlib.h>    /* atoi */

#include <sys/mman.h>  /* mmap */

#include <sys/types.h> /* open */
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>

#ifndef MAP_SYNC
#define MAP_SYNC	0x80000
#endif

#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE	0x03
#endif

#define MAX_TEST_TRIALS 128
#define DEFAULT_TEST_TRIALS 10
static uint16_t test_trials = DEFAULT_TEST_TRIALS;

#define MAX_MSG_SIZE 8192
#define DEFAULT_MSG_SIZE 64
static uint16_t test_msg_size = DEFAULT_MSG_SIZE;

#define SYS_PAGESIZE 4096
static unsigned server_mem_len = SYS_PAGESIZE * 256;

#define TEST_STRING_LEN sizeof(test_message)
#define DEFAULT_PORT    13337
#define IP_STRING_LEN   50
#define PORT_STRING_LEN 8

const char test_message[]   = "UCX Client-Server Hello World";
static uint16_t server_port = DEFAULT_PORT;

/* Test pmem with a file created on pmem device */
const char pmem_test_file[] = "/mnt/pmemd/tmp/ucxfile";

/* Test uncached mem with phys_mem_alloc device */
const char uncached_test_file[] = "/dev/phys_mem_alloc";

/* Test regular mapped file with file created in /tmp */
const char tmp_test_file[] = "/tmp/ucp_mapdev_testfile";

#define TEST_INVALID 0
#define TEST_ALLOC 1
#define TEST_UNCACHED 2
#define TEST_TMP 3
#define TEST_PMEM 4
#define TEST_UDMABUF 5

static uint16_t test_type = TEST_INVALID;

static const char *test_type_str()
{
  switch(test_type) {
  case TEST_INVALID: return "invalid";
  case TEST_ALLOC: return "heap";
  case TEST_UNCACHED: return "uncached";
  case TEST_TMP: return "tempfile";
  case TEST_PMEM: return "pmemfile";
  case TEST_UDMABUF: return "udmabuf";
  default: return "error";
  }
}

static uint16_t parse_test_opt( const char *test_opt ) {
  if (strncmp(test_opt, "heap", 5) == 0) {
    return TEST_ALLOC;
  }
  if (strncmp(test_opt, "tempfile", 9) == 0) {
    return TEST_TMP;
  }
  if (strncmp(test_opt, "pmemfile", 9) == 0) {
    return TEST_PMEM;
  }
  return TEST_INVALID;
}

/**
 * Server context to be used in the user's accept callback.
 * It holds the server's endpoint which will be created upon accepting a
 * connection request from the client.
 */

typedef struct mem_h_info {
  size_t base, len, rkey_len;
} mem_h_info_t;

#define MEM_INFO_SIZE (sizeof(size_t)*3)

typedef struct ucx_server_ctx {
  ucp_ep_h      ep;
  ucp_context_h ucp_context;
  ucp_worker_h  worker;
  ucp_worker_h  data_worker;
  ucp_mem_h     mem_h;
  void          *packed_rkey;
  mem_h_info_t  mem_info;
} ucx_server_ctx_t;

typedef struct ucx_client_ctx {
  ucp_ep_h      ep;
  ucp_worker_h  worker;
  ucp_rkey_h    rkey;
  mem_h_info_t  mem_info;
} ucx_client_ctx_t;
  
void print_diff_time(FILE *fd, struct timeval start_time, struct timeval end_time)
{
    struct timeval diff_time;
    if (end_time.tv_usec < start_time.tv_usec) {
        diff_time.tv_sec  = end_time.tv_sec  - start_time.tv_sec  - 1;
        diff_time.tv_usec = end_time.tv_usec - start_time.tv_usec + 1000*1000;
    } else {
        diff_time.tv_sec  = end_time.tv_sec  - start_time.tv_sec ;
        diff_time.tv_usec = end_time.tv_usec - start_time.tv_usec;
    }
    fprintf(fd, "%ld.%06ld sec", diff_time.tv_sec, diff_time.tv_usec);
}

void print_result(FILE *fd, const char *msg,
		  struct timeval start_time, struct timeval end_time)
{
  fprintf (fd, "*[%s]\ttest=%s, msgsize=%u, trials=%u, memsize=%u, result=",
	   msg, test_type_str(), test_msg_size, test_trials, server_mem_len);
  print_diff_time(fd, start_time, end_time);
  fprintf (fd, "\n"); 
}

/*****************************************************************************/
/* Callbacks and supporting structures 
 */

/**
 * Stream request context. Holds a value to indicate whether or not the
 * request is completed.
 */
typedef struct test_req {
    int complete;
} test_req_t;


/**
 * A callback to be invoked by UCX in order to initialize the user's request.
 */
static void request_init(void *request)
{
    test_req_t *req = request;
    req->complete = 0;
}

/**
 * The callback on the receiving side, which is invoked upon receiving the
 * stream message.
 */
static void
stream_recv_cb(void *request, ucs_status_t status, size_t length)
{
    test_req_t *req = request;

    req->complete = length + 1;

    //printf("stream_recv_cb returned with status %d (%s), length: %lu\n",
    //     status, ucs_status_string(status), length);
}

/**
 * The callback on the sending side, which is invoked after finishing sending
 * the stream message.
 */
static void
stream_send_cb(void *request, ucs_status_t status)
{
    test_req_t *req = request;

    req->complete = 1;

    //printf("stream_send_cb returned with status %d (%s)\n",
    //     status, ucs_status_string(status));
}


/**
 * Error handling callback.
 */
static void err_cb(void *arg, ucp_ep_h ep, ucs_status_t status)
{
    printf("error handling callback was invoked with status %d (%s)\n",
           status, ucs_status_string(status));
}

/*****************************************************************************/
/* Send/Recv Utility
 */

static char* sockaddr_get_ip_str(const struct sockaddr_storage *sock_addr,
                                 char *ip_str, size_t max_size)
{
    struct sockaddr_in  addr_in;
    struct sockaddr_in6 addr_in6;

    switch (sock_addr->ss_family) {
    case AF_INET:
        memcpy(&addr_in, sock_addr, sizeof(struct sockaddr_in));
        inet_ntop(AF_INET, &addr_in.sin_addr, ip_str, max_size);
        return ip_str;
    case AF_INET6:
        memcpy(&addr_in6, sock_addr, sizeof(struct sockaddr_in6));
        inet_ntop(AF_INET6, &addr_in6.sin6_addr, ip_str, max_size);
        return ip_str;
    default:
        return "Invalid address family";
    }
}

static char* sockaddr_get_port_str(const struct sockaddr_storage *sock_addr,
                                   char *port_str, size_t max_size)
{
    struct sockaddr_in  addr_in;
    struct sockaddr_in6 addr_in6;

    switch (sock_addr->ss_family) {
    case AF_INET:
        memcpy(&addr_in, sock_addr, sizeof(struct sockaddr_in));
        snprintf(port_str, max_size, "%d", ntohs(addr_in.sin_port));
        return port_str;
    case AF_INET6:
        memcpy(&addr_in6, sock_addr, sizeof(struct sockaddr_in6));
        snprintf(port_str, max_size, "%d", ntohs(addr_in6.sin6_port));
        return port_str;
    default:
        return "Invalid address family";
    }
}


/**
 * Progress the request until it completes.
 */
static ucs_status_t request_wait(ucp_worker_h ucp_worker, test_req_t *request)
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


static inline int
stream_send(ucp_worker_h worker, ucp_ep_h ep, const void *data, size_t len)
{
  test_req_t *request;
  size_t recv_len;
  int ret = 0;
  ucs_status_t status;

  //printf ("Send %lu bytes of data...\n", len);
  request = ucp_stream_send_nb(ep, data, 1,
			       ucp_dt_make_contig(len),
			       stream_send_cb, 0);
  
  status = request_wait(worker, request);
  if (status != UCS_OK){
    fprintf(stderr, "unable to send UCX message (%s)\n",
	    ucs_status_string(status));
    ret = -1;
  }
  //printf ("Sent %lu bytes of data\n", len);
  return ret;
}

static inline int
stream_recv(ucp_worker_h worker, ucp_ep_h ep, void *data, size_t len)
{
    test_req_t *request;
    size_t recv_len;
    int ret = 0;
    ucs_status_t status;

    //printf ("Recv %lu bytes of data...\n", len);
    request = ucp_stream_recv_nb(ep, data, 1,
				 ucp_dt_make_contig(len),
				 stream_recv_cb, &recv_len,
				 UCP_STREAM_RECV_FLAG_WAITALL);

    status = request_wait(worker, request);
    if (status != UCS_OK){
        fprintf(stderr, "unable to recv UCX message (%s)\n",
                ucs_status_string(status));
        ret = -1;
    }
    //printf ("Recv data, recv_len = %lu\n", recv_len);
    return ret;
}

/**
 * Close the given endpoint.
 * Currently closing the endpoint with UCP_EP_CLOSE_MODE_FORCE since we currently
 * cannot rely on the client side to be present during the server's endpoint
 * closing process.
 */
static void ep_close(ucp_worker_h ucp_worker, ucp_ep_h ep)
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
        fprintf(stderr, "failed to close ep %p\n", (void*)ep);
    }
}


/*****************************************************************************/
/* Server
 */

int test_local_buf(unsigned char* buf, unsigned int size, int check)
{
    int chunk = test_msg_size;
    char cbuf[MAX_MSG_SIZE];
    
    int remain = chunk;
    int trials = test_trials;
    int idx, rdx;
    int error_count = 0;

    //Initialize chunk buffer
    for(rdx = 0; rdx < chunk; rdx++) {
      cbuf[rdx] = 1;
    }
    
    if (check == 0) {
      while(--trials > 0) {
	for(idx = 0; idx < size; idx = idx + chunk) {
	  remain = (idx+chunk < size) ? chunk : (size-idx);
	  memcpy(buf + idx, cbuf, remain);
	}
      }
    }
    else {
      while(--trials > 0) {
	for(idx = 0; idx < size; idx = idx + chunk) {
	  remain = (idx+chunk < size) ? chunk : (size-idx);
	  memcpy(cbuf, buf + idx, remain);
	  
	  for(rdx = 0; rdx < remain; rdx++) {
	    if (cbuf[rdx] != 1) {
	      error_count++;
	    }
	  }
	}
      }
    }
    return error_count;
}

void server_buf_test(unsigned char* buf, unsigned int size)
{
  struct timeval start_time, end_time;
  int            error_count;

  printf ("server_buf_test: buffer self test on addr %p, size = %lu\n",
	  buf, size);
  gettimeofday(&start_time, NULL);
  error_count = test_local_buf(buf, size, 0);
  gettimeofday(&end_time, NULL);
  print_result(stdout, "server_self_write", start_time, end_time);

  gettimeofday(&start_time, NULL);
  error_count = test_local_buf(buf, size, 1);
  gettimeofday(&end_time, NULL);
  print_result(stdout, "server_self_read", start_time, end_time);
  printf ("server_buf_test: self check completed with error_count == %d\n",
	  error_count);
}

static int
create_test_file(const char* fname, size_t length)
{
  int fd = open(fname, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    printf ("Could not open file %s\n", fname);
    return fd;
  }

#define TF_CHUNK_SIZE 4096
  size_t remain = length;
  char buf[TF_CHUNK_SIZE];
  memset(buf, 'z', TF_CHUNK_SIZE);

  while (remain > 0) {
    int ret_out = write(fd, buf, (remain > TF_CHUNK_SIZE) ? TF_CHUNK_SIZE : remain);
    remain -= TF_CHUNK_SIZE;
  }

  return fd;
}

static void*
mmap_udmabuf(size_t length, unsigned int sync_mode, int o_sync)
{
  // valid values 0...7, must have write access to sync mode file
  //unsigned int sync_mode = 0;
  // valid values 0,1 select if the O_SYNC flag is passed opening udmabuf device
  //int o_sync = 0;
  
  int fd;
  unsigned char  attr[1024];
  unsigned int   buf_size = length;
  unsigned long  phys_addr;
  int ret = 0;

  printf ("server mmap_udmabuf: udmabuf device buffer size = %lu\n", length);

  const char physaddr_file[] = "/sys/class/udmabuf/udmabuf0/phys_addr";
  const char size_file[] = "/sys/class/udmabuf/udmabuf0/size";
  const char syncmode_file[] = "/sys/class/udmabuf/udmabuf0/sync_mode";
  const char udmabuf_dev[] = "/dev/udmabuf0";

  if ((fd  = open(physaddr_file, O_RDONLY)) == -1) {
    fprintf(stderr, "Fail to open %s\n", physaddr_file);
    goto err;
  }

  read(fd, attr, 1024);
  sscanf(attr, "%x", &phys_addr);
  close(fd);
  
  if ((fd  = open(size_file , O_RDONLY)) == -1) {
    fprintf(stderr, "Fail to open %s\n", size_file);
    ret = -1;
    goto err;
  }

  read(fd, attr, 1024);
  sscanf(attr, "%d", &buf_size);
  close(fd);

  if ((fd  = open(syncmode_file, O_WRONLY)) == -1) {
    fprintf(stderr, "Fail to open %s, cannot set sync mode\n", syncmode_file);

    if ((fd  = open(syncmode_file, O_RDONLY)) == -1) {
      fprintf(stderr, "Fail to open %s for reading\n", size_file);
      ret = -1;
      goto err;
    }

    unsigned int sync_set;
    read(fd, attr, 1024);
    sscanf(attr, "%d", &sync_set);
    close(fd);

    if (sync_set != sync_mode) {
      fprintf(stderr, "Sync mode of udmabuf is set to %d != %d, aborting test\n",
	      sync_set, sync_mode);
      ret = -1;
      goto err;
    }
  }
  else {
    sprintf(attr, "%d", sync_mode);
    write(fd, attr, strlen(attr));
    close(fd);
  }
  
  printf("server mmap_udmabuf: sync_mode=%d, O_SYNC=%d, ", sync_mode, (o_sync)?1:0);
  printf("server mmap_udmabuf: phys_addr=0x%x\n", phys_addr);
  printf("server mmap_udmabuf: size=%d\n", buf_size);

  if ((fd  = open(udmabuf_dev, O_RDWR | (o_sync ? O_SYNC : 0))) == -1) {
    fprintf(stderr, "Fail to open %s\n", udmabuf_dev);
    ret = -1;
    goto err;
  }

  void *addr = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

  return addr;

 err:
  return MAP_FAILED;
}

void *
mmap_pmem_file(size_t length)
{
  printf ("server mmap_pmem_file: Attempting to map file: %s\n", pmem_test_file);
  
  int fd = create_test_file(pmem_test_file, length);
  
  if (fd < 0)
    goto err;
  
  void *addr = mmap(NULL, length, PROT_READ | PROT_WRITE,
		    MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0);

#if 0
  //POSIX says we can close the file after we mem map it...
  //Do we trust this for rdma/pmem?
  close(fd);
#endif
  
  return addr;
  
 err:
  return MAP_FAILED;
}

void *
mmap_tmp_file(size_t length)
{
  printf ("server mmap_tmp_file: Attempting to map file: %s\n", tmp_test_file);
  
  int fd = create_test_file(tmp_test_file, length);
  
  if (fd < 0)
    goto err;
  
  void *addr = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  close(fd);
  
  return addr;
  
 err:
  return MAP_FAILED;
}


static int
server_map_mem(ucp_context_h context, size_t length, ucp_mem_h *mem_handle)
{
  ucs_status_t s;
  void *addr = NULL;
  int fd = -1;
  ucs_status_t status;

  switch(test_type) {
    
  case TEST_ALLOC:
    addr = aligned_alloc(SYS_PAGESIZE, length);

    if (addr == NULL) {
      fprintf (stderr, "Aligned alloc failed\n");
      return -1;
    }
    break;
    
  case TEST_UNCACHED:
    fd = create_test_file(uncached_test_file, length);

    if (fd < 0)
      return -1;

    addr = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    close(fd);
    
    if (addr == MAP_FAILED) {
      fprintf (stderr, "Failed to map uncached test file\n");
      return -1;
    }
    break;

  case TEST_TMP:
    addr = mmap_tmp_file(length);
    if (addr == MAP_FAILED) {
      fprintf (stderr, "Failed to map uncached test file\n");
      return -1;
    }
    break;

  case TEST_PMEM:
    addr = mmap_pmem_file(length);
    if (addr == MAP_FAILED) {
      fprintf (stderr, "Failed to map pmem test file\n");
      return -1;
    }
    break;

  case TEST_UDMABUF:
    {
      unsigned int sync_mode = 1;
      int o_sync = 1;

      addr = mmap_udmabuf(length, sync_mode, o_sync);
      if (addr == MAP_FAILED) {
	fprintf (stderr, "Failed to map udmabuf test file\n");
	return -1;
      }
    }
    break;
    
  default:
    fprintf (stderr, "Unimplemented test type %d\n", test_type);
  };
  
  ucp_mem_map_params_t mp;
  mp.address             = addr;

  mp.field_mask =
    UCP_MEM_MAP_PARAM_FIELD_LENGTH |
    UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
    UCP_MEM_MAP_PARAM_FIELD_FLAGS;

  mp.flags = UCP_MEM_MAP_NONBLOCK;
  //mp.flags = UCP_MEM_MAP_ALLOCATE;
  mp.length = length;

  printf("server_map_mem: map memory with ucp, addr = %p, size = %lu\n",
	 mp.address, (long unsigned)length);

  status = ucp_mem_map(context, &mp, mem_handle);
  if (status != UCS_OK) {
    fprintf (stderr, "Fail to map memory into ucx\n");
    return status;
  }

#if 0
  // This currently failing often and not sure if needed?
    printf ("server_map_mem: Advising mapped memory status in ucp for addr %p\n", mp.address);
    ucp_mem_advise_params_t advise_params;
    advise_params.address  = mp.address;

    advise_params.field_mask =
      UCP_MEM_ADVISE_PARAM_FIELD_ADDRESS |
      UCP_MEM_ADVISE_PARAM_FIELD_LENGTH |
      UCP_MEM_ADVISE_PARAM_FIELD_ADVICE;
    advise_params.length     = length;
    advise_params.advice     = UCP_MADV_WILLNEED;
    
    status = ucp_mem_advise(context, *mem_handle, &advise_params);
    if (status != UCS_OK) {
      fprintf (stderr, "Fail to advise mapped memory in ucx\n");
      return status;
    }
#endif
  
  return status;
}

/**
 * The callback on the server side which is invoked upon receiving a connection
 * request from the client.
 */
int server_do_comm(ucx_server_ctx_t *ctx);

static void server_conn_handle_cb(ucp_conn_request_h conn_request, void *arg)
{
    ucx_server_ctx_t *context = arg;
    ucp_ep_h         ep;
    ucp_ep_params_t  ep_params;
    ucs_status_t     status;
    ucp_worker_h     data_worker;

    data_worker = context->data_worker;

    /* Server creates an ep to the client on the data worker.
     * This is not the worker the listener was created on.
     * The client side should have initiated the connection, leading
     * to this ep's creation */
    ep_params.field_mask      = UCP_EP_PARAM_FIELD_ERR_HANDLER |
                                UCP_EP_PARAM_FIELD_CONN_REQUEST;
    ep_params.conn_request    = conn_request;
    ep_params.err_handler.cb  = err_cb;
    ep_params.err_handler.arg = NULL;

    if (context->ep != NULL) {
      /* Close the endpoint to the client when new client connects */
      //ep_close(data_worker, ep);
      context->ep = NULL;
    }
    
    status = ucp_ep_create(data_worker, &ep_params, &ep);
    if (status != UCS_OK) {
        fprintf(stderr, "failed to create an endpoint on the server: (%s)\n",
                ucs_status_string(status));
        return;
    }

    context->ep = ep;
    /* Client-Server communication via Stream API */
    
    printf("server_conn_handle_cb: handle incoming connection...\n");
    printf("------------------------------\n");
    server_do_comm(context);
    printf("------------------------------\n");
    printf("server_conn_handle_cb: waiting for another connection...\n");
}

/**
 * Set an address for the server to listen on - INADDR_ANY on a well known port.
 */
void
server_listen_addr(struct sockaddr_in *listen_addr)
{
    /* The server will listen on INADDR_ANY */
    memset(listen_addr, 0, sizeof(struct sockaddr_in));
    listen_addr->sin_family      = AF_INET;
    listen_addr->sin_addr.s_addr = INADDR_ANY;
    listen_addr->sin_port        = server_port;
}

/**
 * Initialize the server side. The server starts listening on the set address
 * and waits for its connected endpoint to be created.
 */
static int
start_server(ucx_server_ctx_t *context,
	     ucp_listener_h *listener)
{
    struct sockaddr_in listen_addr;
    ucp_listener_params_t params;
    ucp_listener_attr_t l_attr;
    ucs_status_t status;
    char ip_str[IP_STRING_LEN];
    char port_str[PORT_STRING_LEN];
    
    server_listen_addr(&listen_addr);

    params.field_mask         = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
                                UCP_LISTENER_PARAM_FIELD_CONN_HANDLER;
    params.sockaddr.addr      = (const struct sockaddr*)&listen_addr;
    params.sockaddr.addrlen   = sizeof(listen_addr);
    params.conn_handler.cb    = server_conn_handle_cb;
    params.conn_handler.arg   = context;


    /* Create a listener on the server side to listen on the given address.*/
    status = ucp_listener_create(context->worker, &params, listener);
    if (status != UCS_OK) {
        fprintf(stderr, "failed to listen (%s)\n", ucs_status_string(status));
	return status;
    }

    /* Query the created listener to get the port it is listening on. */
    l_attr.field_mask = UCP_LISTENER_ATTR_FIELD_SOCKADDR;
    status = ucp_listener_query(*listener, &l_attr);
    if (status != UCS_OK) {
        fprintf(stderr, "failed to query the listener (%s)\n",
                ucs_status_string(status));
        ucp_listener_destroy(*listener);
        return status;
    }
    fprintf(stderr, "server is listening on IP %s port %s\n",
            sockaddr_get_ip_str(&l_attr.sockaddr, ip_str, IP_STRING_LEN),
            sockaddr_get_port_str(&l_attr.sockaddr, port_str, PORT_STRING_LEN));
    
    status = server_map_mem(context->ucp_context, server_mem_len,
			    &(context->mem_h));
    if (status != UCS_OK) {
        fprintf(stderr, "failed to map memory (%s)\n", ucs_status_string(status));
	return status;
    }

    ucp_mem_attr_t attr;
    attr.field_mask =
      UCP_MEM_ATTR_FIELD_ADDRESS |
      UCP_MEM_ATTR_FIELD_LENGTH;

    status = ucp_mem_query(context->mem_h, &attr);
    if (status != UCS_OK) {
      fprintf(stderr, "can't query extent of symmetric heap memory\n");
      return status;
    }

    context->mem_info.base = (size_t)attr.address;
    context->mem_info.len = attr.length;
    size_t rkey_len;
    
    status = ucp_rkey_pack(context->ucp_context, context->mem_h,
			   &(context->packed_rkey), &rkey_len);
    if (status != UCS_OK) {
      fprintf(stderr, "failed to pack rkey\n");
      rkey_len = 0;
    }
    context->mem_info.rkey_len = rkey_len;

    printf("start_server: mapped memory info: base %llx, len %lld, rkey_len %lld\n",
	   context->mem_info.base, context->mem_info.len,
	   context->mem_info.rkey_len);

    server_buf_test(attr.address, attr.length);
    
    return status;
}

/**
 * Callback function for communicating with connected client
 */
int server_do_comm(ucx_server_ctx_t *ctx)
{
  //char recv_message[TEST_STRING_LEN]= "";
    size_t length;
    int ret = 0;
    ucp_worker_h worker = ctx->data_worker;
    ucp_ep_h ep = ctx->ep;
    
    char *recv_buf = malloc(TEST_STRING_LEN);

    ret = stream_recv(worker, ep, recv_buf, TEST_STRING_LEN);
    if (ret != 0) goto out;
    
    printf("server_do_comm: initiated with hello message:\n[%s]\n", recv_buf);

    ret = stream_send(worker, ep, &(ctx->mem_info), MEM_INFO_SIZE);
    if (ret != 0) {
      fprintf(stderr, "Failed to send rkey info to client\n");
      goto out;
    }
    
    ret = stream_send(worker, ep, ctx->packed_rkey, ctx->mem_info.rkey_len);
    if (ret != 0) {
      fprintf(stderr, "Failed to send packed rkey to client\n");
      goto out;
    }

    printf ("server_do_comm: completed send mem info and packed rkey\n");
    
out:
    if (recv_buf != NULL) {
      free(recv_buf);
    }
    return ret;
}

int init_worker(ucp_context_h ucp_context, ucp_worker_h *ucp_worker);

static int run_server(ucp_context_h ucp_context, ucp_worker_h ucp_worker)
{
  ucp_listener_h listener;
  ucs_status_t status;
  ucx_server_ctx_t context;
  ucp_worker_h ucp_data_worker;

  /* Initialize the server's endpoint to NULL. Once the server's endpoint
   * is created, this field will have a valid value. */
  context.ep = NULL;
  context.ucp_context = ucp_context;
  context.worker = ucp_worker;

  int ret = init_worker(ucp_context, &ucp_data_worker);
  if (ret != 0) {
    goto err_worker;
  }

  context.data_worker = ucp_data_worker;

  /* Server will create a listener that calls back to a connection handler.
   * @server_conn_handle_cb 
   * which calls @server_do_comm for each new client connection */
  status = start_server(&context, &listener);
  if (status != UCS_OK) {
    fprintf(stderr, "failed to start server\n");
    return -1;
  }

  /* Server is always up */
  printf("Waiting for connection...\n");
  while (1) {
    ucp_worker_progress(ucp_worker);
  }
    
  return 0;

 err_worker:
  return -1;
}

/*****************************************************************************/
/* Client
 */

void
c2s_ucp_put(ucx_client_ctx_t *ctx, size_t offset, const void *src, size_t nbytes)
{
  ucs_status_t status;

  if ((offset + nbytes) > ctx->mem_info.len) {
    printf ("Invalid put offset+nbytes %lu into mem region size %lu\n",
	    offset+nbytes, ctx->mem_info.len);
    return;
  }
  
  
  uint64_t r_dest = ctx->mem_info.base + offset;
  
  //printf ("ucp_put src=%p, nbytes=%lld, offset=%lld, r_dest=%llx\n",
  //	  src, nbytes, offset, r_dest);
  
  status = ucp_put(ctx->ep, src, nbytes, r_dest, ctx->rkey);
  if (status != UCS_OK) {
    fprintf (stderr, "Failed to put data to server: %s\n",
	     ucs_status_string(status));
  }
}

void
c2s_ucp_get(ucx_client_ctx_t *ctx, size_t offset, void *dest, size_t nbytes)
{
  ucs_status_t status;

  if ((offset + nbytes) > ctx->mem_info.len) {
    printf ("Invalid get offset+nbytes %lu into mem region size %lu\n",
	    offset+nbytes, ctx->mem_info.len);
    return;
  }
  
  uint64_t r_src = ctx->mem_info.base + offset;

  //printf ("ucp_get dest=%p, nbytes=%lld, offset=%lld, r_src=%llx\n",
  //	  dest, nbytes, offset, r_src);
  
  status = ucp_get(ctx->ep, dest, nbytes, r_src, ctx->rkey);
  if (status != UCS_OK) {
    fprintf (stderr, "Failed to get data from server: %s\n",
	     ucs_status_string(status));
  }
}


int test_remote_buf(ucx_client_ctx_t *ctx, size_t offset, size_t size, int check)
{
  const int chunk = test_msg_size;
  char cbuf[MAX_MSG_SIZE];
  
  int remain = chunk;
  int trials = test_trials;
  int idx, rdx;
  int error_count = 0;

  //Initialize chunk buffer
  for(rdx = 0; rdx < chunk; rdx++) {
    cbuf[rdx] = (rdx & 0xFF);
  }
  
  if (check == 0) {
    while(--trials > 0) {
      for(idx = 0; idx < size; idx = idx + chunk) {
	remain = (idx+chunk < size) ? chunk : (size-idx);
	c2s_ucp_put(ctx, offset + idx, cbuf, remain);
      }
    }
  }
  else {
    while(--trials > 0) {
      for(idx = 0; idx < size; idx = idx + chunk) {
	remain = (idx+chunk < size) ? chunk : (size-idx);
	c2s_ucp_get(ctx, offset + idx, cbuf, remain);

	for(rdx = 0; rdx < remain; rdx++) {
	  if (cbuf[rdx] != (rdx & 0xFF)) {
	    error_count++;
	  }
	}
      }
    }
  }
  return error_count;
}

void client_buf_test(ucx_client_ctx_t *ctx)
{
  struct timeval start_time, end_time;
  int            error_count;

  size_t size = ctx->mem_info.len;

  printf ("client_buf_test: buffer write test on remote region, size = %lu\n",
	  size);
  gettimeofday(&start_time, NULL);
  error_count = test_remote_buf(ctx, 0, size, 0);
  gettimeofday(&end_time, NULL);
  print_result(stdout, "client_write_rdma", start_time, end_time);
  
  gettimeofday(&start_time, NULL);
  error_count = test_remote_buf(ctx, 0, size, 1);
  gettimeofday(&end_time, NULL);
  print_result(stdout, "client_read_rdma", start_time, end_time);

  printf ("client_buf_test: check completed with error_count == %d\n",
	  error_count);
}

/**
 * Set an address to connect to. A given IP address on a well known port.
 */
void client_connect_addr(const char *address_str, struct sockaddr_in *connect_addr)
{
    memset(connect_addr, 0, sizeof(struct sockaddr_in));
    connect_addr->sin_family      = AF_INET;
    connect_addr->sin_addr.s_addr = inet_addr(address_str);
    connect_addr->sin_port        = server_port;
}

/**
 * Initialize the client side. Create an endpoint from the client side to be
 * connected to the remote server (to the given IP).
 */
static int start_client(ucx_client_ctx_t *ctx, const char *ip)
{
    ucp_ep_params_t ep_params;
    struct sockaddr_in connect_addr;
    ucs_status_t status;
    ucp_ep_h *client_ep = &(ctx->ep);
    ucp_worker_h ucp_worker = ctx->worker;

    client_connect_addr(ip, &connect_addr);

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
    ep_params.field_mask       = UCP_EP_PARAM_FIELD_FLAGS     |
                                 UCP_EP_PARAM_FIELD_SOCK_ADDR |
                                 UCP_EP_PARAM_FIELD_ERR_HANDLER; // |
    //                                 UCP_EP_PARAM_FIELD_ERR_HANDLING_MODE;
    //    ep_params.err_mode         = UCP_ERR_HANDLING_MODE_PEER;
    ep_params.err_handler.cb   = err_cb;
    ep_params.err_handler.arg  = NULL;
    ep_params.flags            = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
    ep_params.sockaddr.addr    = (struct sockaddr*)&connect_addr;
    ep_params.sockaddr.addrlen = sizeof(connect_addr);

    status = ucp_ep_create(ucp_worker, &ep_params, client_ep);
    if (status != UCS_OK) {
        fprintf(stderr, "failed to connect to %s (%s)\n", ip, ucs_status_string(status));
    }

    return status;
}

/**
 * Send and receive a message using the Stream API.
 * The client sends a message to the server and waits until the send it completed.
 */
static int client_do_comm(ucx_client_ctx_t *ctx)
{
    int ret = 0;
    ucp_worker_h worker = ctx->worker;
    ucp_ep_h ep = ctx->ep;
    ucs_status_t status;

    ret = stream_send(worker, ep, test_message, TEST_STRING_LEN);
    if (ret != 0) goto out;

    printf ("client_do_comm: sent hello message:\n[%s]\n", test_message);

    ret = stream_recv(worker, ep, &(ctx->mem_info), MEM_INFO_SIZE);
    if (ret != 0) {
      fprintf(stderr, "Failed to recv rkey info to client\n");
      goto out;
    }

    void *packed_rkey = malloc(ctx->mem_info.rkey_len);
    
    ret = stream_recv(worker, ep, packed_rkey, ctx->mem_info.rkey_len);
    if (ret != 0) {
      fprintf(stderr, "Failed to recv packed rkey on client\n");
      goto out_rkey;
    }

    printf ("client_do_comm: completed recv mem info and packed rkey\n");
    
    ucp_ep_rkey_unpack(ep, packed_rkey, &(ctx->rkey));

    client_buf_test(ctx);

 out_rkey:
    free(packed_rkey);

    ucp_rkey_destroy(ctx->rkey);

 out:
    return ret;
}

int run_client(char *server_addr, ucp_worker_h ucp_worker)
{
  ucs_status_t status;
  int ret = -1;
  ucx_client_ctx_t context;
  context.worker = ucp_worker;

  status = start_client(&context, server_addr);
  if (status != UCS_OK) {
    fprintf(stderr, "failed to start client\n");
    return ret;
  }
  
  /* Client-Server communication via Stream API */
  printf("run_client: handle connection...\n");
  printf("------------------------------\n");
  ret = client_do_comm(&context);
  printf("------------------------------\n");
  printf("run_client: closing connection...\n");
  
  /* Close the endpoint to the server */
  ep_close(ucp_worker, context.ep);

  return ret;
}

/**
 * Print this application's usage help message.
 */
static void usage()
{
    fprintf(stderr, "Usage: ucp_client_server [parameters]\n");
    fprintf(stderr, "UCP client-server example utility\n");
    fprintf(stderr, "\nParameters are:\n");
    fprintf(stderr, " -a Set IP address of the server "
                    "(required for client and should not be specified "
                    "for the server)\n");
    fprintf(stderr, " -p Port number to listen/connect to (default = %d). "
                    "0 on the server side means select a random port and print it\n",
                    DEFAULT_PORT);
    fprintf(stderr, " -t Test to run, one of: [heap,tempfile,pmemfile]\n");
    fprintf(stderr, " -N number of test iterations (default = %d, max = %d)\n",
	    DEFAULT_TEST_TRIALS, MAX_TEST_TRIALS);
    fprintf(stderr, " -S number of bytes to read/write per get/put operation (default = 64)\n");
}

/**
 * Parse the command line arguments.
 */
static int parse_cmd(int argc, char *const argv[], char **server_addr)
{
    int c = 0;
    opterr = 0;

    while ((c = getopt(argc, argv, "a:p:t:N:S:")) != -1) {
        switch (c) {
	case 't':
	  test_type = parse_test_opt(optarg);
	  break;
	case 'N':
	  test_trials = atoi(optarg);
	  if (test_trials > MAX_TEST_TRIALS) {
	    test_trials = DEFAULT_TEST_TRIALS;
	    fprintf (stderr,
		     "Invalid value for number of trials. Set to default %d\n",
		     test_trials);
	  }
	  break;
	case 'S':
	  test_msg_size = atoi(optarg);
	  if (test_msg_size > MAX_MSG_SIZE) {
	    test_msg_size = DEFAULT_MSG_SIZE;
	    fprintf(stderr,
		    "Value for message size exceeds max size. Set to default %d\n",
		    test_msg_size);
	  }
	  break;
        case 'a':
            *server_addr = optarg;
            break;
        case 'p':
            server_port = atoi(optarg);
            if (server_port < 0) {
                fprintf(stderr, "Wrong server port number %d\n", server_port);
                return -1;
            }
            break;
        default:
            usage();
            return -1;
        }
    }

    if (test_type == TEST_INVALID) {
      fprintf(stderr, "Must specify valid test type\n");
      usage();
      return -1;
    }
    
    return 0;
}

/**
 * Create a ucp worker on the given ucp context.
 */
int init_worker(ucp_context_h ucp_context, ucp_worker_h *ucp_worker)
{
    ucp_worker_params_t worker_params;
    ucs_status_t status;
    int ret = 0;

    memset(&worker_params, 0, sizeof(worker_params));

    worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_params.thread_mode = UCS_THREAD_MODE_SINGLE;

    status = ucp_worker_create(ucp_context, &worker_params, ucp_worker);
    if (status != UCS_OK) {
        fprintf(stderr, "failed to ucp_worker_create (%s)\n", ucs_status_string(status));
        ret = -1;
    }

    return ret;
}

/**
 * Initialize the UCP context and worker.
 */
static int init_context(ucp_context_h *ucp_context, ucp_worker_h *ucp_worker)
{
    /* UCP objects */
    ucp_params_t ucp_params;
    ucs_status_t status;
    int ret = 0;

    /* UCP initialization */
    memset(&ucp_params, 0, sizeof(ucp_params));

    ucp_params.field_mask   = UCP_PARAM_FIELD_FEATURES     |
                              UCP_PARAM_FIELD_REQUEST_SIZE |
                              UCP_PARAM_FIELD_REQUEST_INIT;

    // AMO features cause failure on bluefield
    ucp_params.features     = UCP_FEATURE_RMA      |
      UCP_FEATURE_AMO32    |  
      UCP_FEATURE_AMO64    |  
      UCP_FEATURE_STREAM;

    ucp_params.request_size = sizeof(test_req_t);
    ucp_params.request_init = request_init;

    status = ucp_init(&ucp_params, NULL, ucp_context);
    if (status != UCS_OK) {
        fprintf(stderr, "failed to ucp_init (%s)\n", ucs_status_string(status));
        ret = -1;
        goto err;
    }


    ret = init_worker(*ucp_context, ucp_worker);
    if (ret != 0) {
      goto err_cleanup;
    }
    
    return ret;

err_cleanup:
    ucp_cleanup(*ucp_context);

err:
    return ret;
}


int main(int argc, char **argv)
{
  int ret;
    char *server_addr = NULL;

    /* UCP objects */
    ucp_context_h ucp_context;
    ucp_worker_h ucp_worker;


    ret = parse_cmd(argc, argv, &server_addr);
    if (ret != 0) {
        goto err;
    }

    /* Initialize the UCX required objects */
    ret = init_context(&ucp_context, &ucp_worker);
    if (ret != 0) {
        goto err;
    }

    /* Client-Server initialization */
    if (server_addr == NULL) {
      run_server(ucp_context, ucp_worker);
    } else {
      run_client(server_addr, ucp_worker);
    }

    ucp_worker_destroy(ucp_worker);
    ucp_cleanup(ucp_context);
    
err:
    return ret;
}
