/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef _SHMEMIO_H
#define _SHMEMIO_H 1

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
//#include <errno.h>

#include <ucp/api/ucp.h>

#include "khash.h"

KHASH_MAP_INIT_STR(str2ptr, void*);
KHASH_MAP_INIT_INT64(ptr2ptr, void*);

#include "allocator/dlmalloc.h"

/*** Threading support 
 * This duplicates some of the defines in shmemc/threading
 * because include shmemc would require building the 
 * standalone server against all of libshmemc, which brings 
 * a bunch of deps like pmix, etc that we don't want
 * for the standalone server
 *
 * This could get merged in to some utility threading library
 * with no other external deps
 *
 ***/
#include <pthread.h>

#define shmemio_mutex_t pthread_mutex_t
#define shmemio_mutex_lock(_mut_) pthread_mutex_lock(_mut_)
#define shmemio_mutex_unlock(_mut_) pthread_mutex_unlock(_mut_)
#define shmemio_mutex_trylock(_mut_) pthread_mutex_trylock(_mut_)
#define shmemio_mutex_init(_mut_,_attr_) pthread_mutex_init(_mut_,_attr_)
#define shmemio_mutex_destroy(_mut_) pthread_mutex_destroy(_mut_)

#define shmemio_seterr(_err_, _val_) { if ((_err_) != NULL) *(_err_) = (_val_); }

// This is hardcoded for prototype server. Multiprocess server should
// be developed in tandem with OpenSHMEM server API for best use of effort
#define SHMEMIO_SINGLE_SERVER_PROCESS

//Forward declarations
typedef struct shmemc_context *shmemc_context_h;

typedef struct shmem_fspace_conx_s shmem_fspace_conx_t;

/************************ begin FILE POINTER STRUCTURES ***********************/

typedef struct shmemio_fp_s {
    /* do not move fields around in this struct */
  void *addr;
  size_t size;
  int unit_size;
  int pe_start;
  int pe_stride;
  int pe_size;

  uint64_t fkey;

  time_t ctime; //time the file was loaded into current location
  time_t atime; //time of last file open
  time_t mtime; //time of last ftrunc or fextend
  time_t ftime; //time of last flush
  
  size_t offset; // offset in this region
  int l_region;  // so we don't have to look up region with address
  int fspace;    // so we don't have to look up fspace with pe
  
#ifdef ENABLE_DEBUG
  struct shmemio_fp_s *next_active;
  struct shmemio_fp_s *prev_active;
#endif
} shmemio_fp_t;


typedef struct shmemio_fp_list_s {
  /* do not move fields around in this struct */
  struct shmemio_fp_list_s *next;
  uint64_t flags; /* currently unused */
} shmemio_fp_list_t;

typedef struct shmemio_sfile_s {
  char *sfile_key;
  
  size_t offset;
  size_t size;
  int region_id;
  int has_backing_file;
  int mark_for_unload;

  int open_count, close_waitc;
  void *blocking_nonclose;
  uint64_t blocking_data;
  void **iowait;
  size_t iowait_len;

  time_t ctime; //Time the file was put at its current location and region in the filespace 
  time_t atime; //Last time opened
  time_t mtime; //Last time ftrunc or fextend 
  time_t ftime; //Last time flush to persistence
  
} shmemio_sfile_t;

/************************ end FILE POINTER STRUCTURES ***********************/


#define shmemio_CAT(a, b) a##b
#define shmemio_CCAT(a, b) CAT(a, b)

#include <assert.h>

#ifdef static_assert
#define shmemio_static_assert static_assert
#elif defined(__COUNTER__)
#define shmemio_static_assert(e,m)					\
  ;enum { shmemio_CCAT(static_assert_, __COUNTER__) = 1/(int)(!!(e)) }
#else
#define shmemio_static_assert(e,m)					\
  ;enum { shmemio_CCAT(assert_line_, __LINE__) = 1/(int)(!!(e)) }
#endif

/************************ begin COMM STRUCTURES ***********************/

typedef enum {
  shmemio_success = 0,
  shmemio_action_blocked = -1,
  shmemio_err_unknown = -2,
  shmemio_err_region_create = -3,
  shmemio_err_region_req = -4,
  shmemio_err_shared_resize = -5,
  shmemio_err_resize = -6,
  shmemio_err_resize_norelo = -7,
  shmemio_err_doubleblock = -8,
  shmemio_err_nodeadlock = -9,
  shmemio_err_invalid = -10,
  shmemio_err_send = -11,
  shmemio_err_recv = -12,
  shmemio_num_errtypes = 13
} shmemio_err_code_t;

static const char*
shmemio_err2str(int e, int *len) {

  static const char* estr[] = {
    "Success",
    "Previous action blocked",
    "Unclassified Error",
    "Could not create region for given request",
    "Client did not receive required region info",
    "Truncate failed due to shared file access",
    "File could not be resized, likely OOM in region",
    "File resize failed because file cannot be relocated",
    "File pointer attempted to be used to block more than once simultaneously",
    "More than one file access tried to io wait without closing file",
    "Invalid argument or parameter",
    "Failed to send data",
    "Failed to receive data"
  };

  if ((-e >= 0) && (-e < shmemio_num_errtypes)) {
    if (len != NULL) {
      *len = strlen(estr[-e]);
    }
    return estr[-e];
  }
  
  *len = strlen("Invalid error number");
  return "Invalid error number";
}
  

typedef enum {
  shmemio_fclose_req = 1,
  shmemio_fp_flush_req = 2,
  shmemio_ftrunc_req = 3,
  shmemio_fopen_req = 4,
  shmemio_fspace_stat_req = 5,
  shmemio_fspace_flush_req = 6,
  shmemio_fp_stat_req = 7,
  shmemio_fextend_req = 8,
  shmemio_region_req = 9,
  shmemio_disco_req = 10,
  shmemio_total_req_c = 11,
} shmemio_req_type_t;


static const char*
shmemio_rt2str(int rt) {

  static const char* rtstr[] = {
    "",
    "file close",
    "file flush",
    "file trunc",
    "file open",
    "fspace stat",
    "fspace flush",
    "file stat",
    "file extend",
    "region request",
    "disconnect"
  };

  if (rt < shmemio_total_req_c) {
    return rtstr[rt];
  }
  return "invalid request type";
};


#define SHMEMIO_REQ_TYPE_BITS 4

#define shmemio_req_t_payload_size (128 - (2 * sizeof(short)))

typedef struct shmemio_req_s {
  short type;
  short status;
  char payload[shmemio_req_t_payload_size];
} shmemio_req_t;

typedef struct shmemio_fopen_req_s {
  size_t fsize;
  int sfpe_start, sfpe_stride, sfpe_size, unit_size;
  size_t file_path_len;
  int l_region;  //which region id
  size_t offset; //what is the offset within the region
  uint64_t fkey;
} shmemio_fopen_req_t;

shmemio_static_assert( (sizeof(shmemio_fopen_req_t) < shmemio_req_t_payload_size),
		       "Misconfigured request payload size for fopen request" );

typedef struct shmemio_fp_req_s {
  uint64_t fkey;
  int ioflags;
  size_t size;
  size_t offset;
} shmemio_fp_req_t;

shmemio_static_assert( (sizeof(shmemio_fp_req_t) < shmemio_req_t_payload_size), "Misconfigured request payload size for fclose request" );


typedef struct shmemio_fp_stat_s {
  size_t size;
  time_t ctime; //time the file was loaded into current location
  time_t atime; //time of last file open
  time_t mtime; //time of last ftrunc or fextend
  time_t ftime; //time of last flush
} shmemio_fp_stat_t;

typedef struct shmemio_connreq_s {
  int nfpes;
  int nregions;
} shmemio_connreq_t;

typedef struct shmemio_conn_s shmemio_conn_t;
typedef struct shmemio_sfile_ls_s shmemio_sfile_ls_t;

struct shmemio_sfile_ls_s {
  shmemio_sfile_t *sfile;
  shmemio_conn_t *conn;
  unsigned waitcond;
  shmemio_sfile_ls_t *next, *prev;
};

typedef struct shmemio_conn_s {
  int nfpes;
  int nregions;
  int acked;
  int flags;
  ucp_ep_h ep;
  shmemio_sfile_ls_t *open_sfiles;

  volatile shmemio_conn_t *next, *prev;
} shmemio_conn_t;

typedef struct shmemio_server_s shmemio_server_t;
typedef void (*shmemio_conn_cb_t)(shmemio_server_t*, shmemio_conn_t*, void*);

/**
 * Stream request context. Holds a value to indicate if request completed
 */
typedef struct shmemio_streamreq_s {
    volatile size_t complete;
} shmemio_streamreq_t;

/**
 * A callback to be invoked by UCX in order to initialize the user's request.
 */
static void
shmemio_request_init(void *request)
{
    shmemio_streamreq_t *req = request;
    req->complete = 0;
}



/************************ end COMM STRUCTURES ***********************/


/************************ begin CLIENT DATA STRUCTURES ***********************/

typedef struct shmemio_client_fpe_s {
  int valid;
  int fspace;                      // this fpe's fspace index

  /* fields set on connect */
  size_t          server_addr_len;
  ucp_address_t  *server_addr;     // fpe worker address on storage server

  /* fields set on fill */
  ucp_ep_h server_ep;              // create this endpoint from addr sent back from server

} shmemio_client_fpe_t;


// remote region access information
typedef struct shmemio_remote_region_s {

  /* fields set on connect */
  size_t          r_base, r_end;      // server remote address ranges
  size_t          len;                // server remote memory space size
  size_t          rkey_len;
  void           *packed_rkey;        //packed remote access key

  /* fields set on fill */
  ucp_rkey_h      rkey;               //unpacked server filespace rkey
} shmemio_remote_region_t;


// symmetric remote memory address region
typedef struct shmemio_client_region_s {

  /* fields set on fill */
  size_t l_base, l_end;      // local address region

  /* fields set on connect */
  size_t len;                // remote memory region space size
  
  // fpe set where this parallel memory region lives
  int fpe_start, fpe_stride, fpe_size;   // indexing into client fpe array, not using global pe index
  int unit_size;
  
  shmemio_remote_region_t *r_regions;    // remote region array of size fpe_size
  
} shmemio_client_region_t;

// open connection to fspace server
typedef struct shmemio_fspace_s {
  int valid;
  shmemc_context_h ch;        // context to use for I/O

  /* fields set on fill */
  ucp_ep_h       req_ep;      //where to send requests (fopen, flush, etc)

  /* fields set on connect */
  int nfpes;
  int fpe_start, fpe_end;    // indexing into client fpe array, not global pe index

  /* fields may change on fopen */
  int nregions;
  shmemio_client_region_t *l_regions;

  size_t *used_addrs;
  int ua_len, ua_max;

} shmemio_fspace_t;

/************************ end CLIENT DATA STRUCTURES ***********************/

/************************ begin SERVER DATA STRUCTURES ***********************/

typedef struct shmemio_sfpe_mem_s {
  size_t          base, end, len;
  size_t          rkey_len;

  void           *packed_rkey;
  ucp_mem_h       mem_handle;
  ucp_mem_attr_t  attr;
} shmemio_sfpe_mem_t;


typedef struct shmemio_server_region_s {
  //sfpe set that this region applies to
  int             sfpe_start, sfpe_stride, sfpe_size;
  int             unit_size;

  size_t          mem_len;
  mspace          mem_space;
  
  // parallel memory region array of size sfpe_size
  // like a team heap where the sfpe set is the team
  shmemio_sfpe_mem_t *sfpe_mems;

} shmemio_server_region_t;


typedef struct shmemio_server_fpe_s {
  size_t          worker_addr_len;
  ucp_address_t  *worker_addr;

} shmemio_server_fpe_t;


typedef enum {
  shmemio_server_err     = 0,
  shmemio_server_init    = 1,
  shmemio_server_listen  = 2,
  shmemio_server_running = 3,
  shmemio_server_halting = 4,
  shmemio_server_stopped = 5
} shmemio_server_status_t;


typedef struct shmemio_server_s {
  ucp_context_h   context;

  // The server currently uses single worker
  ucp_worker_h  worker;
  
  int           nsfpes;
  shmemio_server_fpe_t *sfpes;

  int           maxregions, nregions;
  shmemio_server_region_t *regions;

  int nfiles;
  khash_t(str2ptr) *l_file_hash;
  khash_t(ptr2ptr) *cli_conn_hash;
  
  ucp_listener_h listener;

  shmemio_mutex_t          cli_conn_ls_lock;
  volatile shmemio_conn_t *cli_conns;

  shmemio_mutex_t          req_conn_ls_lock;
  volatile shmemio_conn_t *req_conns;

  shmemio_conn_cb_t conn_cb_f;
  void             *conn_cb_args;

  //shmemio_mutex_t  status_lock;
  volatile shmemio_server_status_t  status;
  
  size_t          sys_pagesize;
  uint16_t        port;

  size_t          default_unit, default_len;
  
} shmemio_server_t;

/************************ end SERVER DATA STRUCTURES ***********************/





/************************ TRANSLATION FUNCTIONS ***********************/

/*
 * go from global pe index to fpe array index
 */
#define pe_to_fpe_index(_pe_) (_pe_ - proc.nranks)

/*
 * go from fpe index to sfpe array index on given fspae
 */
#define fpe_to_sfpe_index(_fio_,_fpe_) (_fpe_ - (_fio_).fpe_start)

/*
 * go from global pe index to fpe array index
 */
#define fpe_to_pe_index(_fpe_) (_fpe_ + proc.nranks)

/*
 * go from fspace sfpe array index to local fpe array index
 */
#define sfpe_to_fpe_index(_fio_,_sfpe_) (_sfpe_ + (_fio_).fpe_start)


#define total_n_pes() (proc.nranks + proc.io.nfpes)


int shmemio_addr_accessible(const void *addr, int pe);

void secondary_remote_key_and_addr(uint64_t local_addr, int pe,
				   ucp_rkey_h *rkey_p, uint64_t *raddr_p);



/************************ CLIENT FUNCTIONS ***********************/

/* io.c */

void shmemio_init_client();

void shmemio_finalize_client();


/************************ SERVER FUNCTIONS ***********************/

#define SHMEMIO_EXPORT_ONLY
#include "shmemio_server.h"


#ifdef ENABLE_DEBUG

/*
 * sanity checks
 */
# define SHMEMIO_CHECK_PE_ARG_RANGE(_pe, _argpos)               \
    do {                                                        \
      const int top_pe = total_n_pes() - 1;			\
      								\
      if ((_pe < 0) || (_pe > top_pe)) {			\
	logger(LOG_FATAL,					\
	       "In %s(), PE argument #%d is %d: "		\
	       "outside allocated range [%d, %d]",		\
	       __func__,					\
	       _argpos,						\
	       _pe,						\
	       0, top_pe					\
	       );						\
	/* NOT REACHED */					\
      }								\
    } while (0)

# define SHMEMIO_CHECK_ACCESSIBLE(_addr, _argpos1, _pe, _argpos2)	\
    do {                                                        \
        if (!shmemc_addr_accessible(_addr, proc.rank) && !shmemio_addr_accessible(_addr, _pe)) {       \
            logger(LOG_FATAL,                                   \
                   "In %s(), address %p in argument #%d "       \
                   "is not accessible on pe %d in argument #%d",	\
                   __func__,                                    \
                   _addr, _argpos1,				\
	           _pe, _argpos2,				\
                   );                                           \
            /* NOT REACHED */                                   \
        }                                                       \
    } while (0)

#else  /* ! ENABLE_DEBUG */

# define SHMEMIO_CHECK_PE_ARG_RANGE(_pe, _argpos)
# define SHMEMIO_CHECK_ACCESSIBLE(_addr, _argpos1, _pe, _argpos2)

#endif /* ENABLE_DEBUG */

#endif
