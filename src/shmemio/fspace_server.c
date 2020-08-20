/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#include "shmemio.h"
#include "shmemio_test_util.h"

#include <ucp/api/ucp.h>
#include <ucs/config/global_opts.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  /* getopt */
#include <ctype.h>   /* isprint */
#include <pthread.h> /* pthread_self */
#include <errno.h>   /* errno */
#include <time.h>
#include <signal.h>  /* raise */
#include <fcntl.h>

#define ENABLE_SERVER_THREADS
//#define ENABLE_MULTIPLE_WORKERS

#define CHKERR_JUMP(_cond, _msg, _label)            \
do {                                                \
    if (_cond) {                                    \
        fprintf(stderr, "Failed to %s\n", _msg);    \
        goto _label;                                \
    }                                               \
} while (0)

struct msg {
    uint64_t        data_len;
};

static const size_t sys_pagesize = 65536;

typedef struct local_state_s local_state_t;
typedef struct wt_state_s    wt_state_t;

typedef enum {
  wt_state_err = 0,
  wt_state_new = 1,
  wt_state_running = 2,
  wt_state_halting = 3
} wt_status_t;

typedef struct wt_state_s {
  volatile wt_status_t  status;
  pthread_t             pth;

  shmemio_server_t  *server;
  shmemio_conn_t    *conn;

  volatile wt_state_t  *prev, *next;
} wt_state_t;

typedef struct local_state_s {
  int daemonize;
  
  ucp_context_h     context;

  int               nworkers;
  ucp_worker_h     *workers;

  volatile int         no_new_wt;
  volatile int         nwt;
  volatile wt_state_t *wts;

  pthread_mutex_t   wt_status_lock;
  pthread_mutex_t   state_ls_lock;

  int               nsfpes;
  int               log_level;
  
  uint16_t          port;

  size_t            sys_pagesize;
  size_t            region_size;
  size_t            default_unit;

  shmemio_server_t  server;

} local_state_t;

local_state_t loc;

static int parse_cmd(int argc, char * const argv[], local_state_t *loc);

void sig_handler(int signo)
{
  if ( (signo == SIGINT) || (signo == SIGUSR1) ) {
    printf ("Test server: Halting server\n");

    loc.server.status = shmemio_server_halting;
    ucp_worker_signal(loc.server.worker);
  }
}

static void daemonize(void)
{
    pid_t pid, sid;
    int fd; 

    /* already a daemon */
    if ( getppid() == 1 ) return;

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0)  
    {   
        exit(EXIT_FAILURE);
    }   

    if (pid > 0)  
    {   
        exit(EXIT_SUCCESS); /*Killing the Parent Process*/
    }   

    /* At this point we are executing as the child process */

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0)  
    {
        exit(EXIT_FAILURE);
    }

    if (signal(SIGINT, sig_handler) == SIG_ERR) {
      exit(EXIT_FAILURE);
    }
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
      exit(EXIT_FAILURE);
    }

    
    /* Change the current working directory. */
    if ((chdir("/")) < 0)
    {
        exit(EXIT_FAILURE);
    }


    fd = open("/dev/null",O_RDWR, 0);

    if (fd != -1)
    {
        dup2 (fd, STDIN_FILENO);
        dup2 (fd, STDOUT_FILENO);
        dup2 (fd, STDERR_FILENO);

        if (fd > 2)
        {
            close (fd);
        }
    }

    /*resettign File Creation Mask */
    umask(027);
}

void print_region_bytes(shmemio_server_region_t *reg, size_t offset, size_t pbytes)
{
  size_t unit_num = (offset / reg->unit_size) * reg->unit_size;
  size_t bytes_left = pbytes;

  while (bytes_left > 0) {

    size_t bytes = (bytes_left > reg->unit_size) ? reg->unit_size : bytes_left;

    int sfpeid = reg->sfpe_start;
    for (int idx = 0; idx < reg->sfpe_size; idx++) {
      printf ("------------ sfpe %d -------------\n", sfpeid);
      print_bytes((char*)(reg->sfpe_mems[idx].base + offset), bytes);
      printf ("\n----------------------------------\n");
      sfpeid += reg->sfpe_stride;
    }

    offset += bytes;
    bytes_left -= bytes;
  }
}

int get_int_input()
{
  char buf[100];
  printf ("Enter a number: ");
  scanf ("%[^\n]%*c", buf);
  return atoi(buf);
}

#define CHK_ERR_JUMP(_test_, _tag_, ...) \
  if (_test_) { printf (__VA_ARGS__); goto _tag_; }

char *trimws(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}



void interactive_server_test(shmemio_server_t *srvr)
{
  printf ("\n----------- basic_ucx_server_test ------------\n");
  
  if (srvr->nregions < 1) {
    printf ("Cannot run test with nregions = %d\n", srvr->nregions);
    goto alldone;
  }

  size_t pstart = 0;
  size_t pbytes = 64;
  int regid = 0;
  int done = 0;

  while (!done) {
    char buffer[1024];

    shmemio_server_region_t *reg = &(srvr->regions[regid]);

    printf ("Server info: [%d regions, %d sfpes]\n", srvr->nregions, srvr->nsfpes);
    printf ("\tcurrent region      = %d\n", regid);
    printf ("\t        unit size   = %d\n", reg->unit_size);
    printf ("\t        pes         = [%d:%d by %d]\n",
	    reg->sfpe_start, reg->sfpe_start + reg->sfpe_size, reg->sfpe_stride);
    printf ("\tcurrent bytes range = %x:%x (len=%u)\n",
	    pstart, pstart+pbytes, pbytes);
    
    printf ("\nEnter command:\n");
    printf ("\tdone       : shutdown server\n");
    printf ("\tprint      : print region bytes\n");
    printf ("\tset region : set region number\n");
    printf ("\tset start  : set start byte\n");
    printf ("\tset bytes  : set how many bytes to print\n");
    printf (">\n");
    //scanf ("%[^\n]%*c", buf);
    char* buf = fgets(buffer, sizeof(buffer), stdin);
    buf = trimws(buf);
    
    if (strcmp(buf, "done") == 0)
      done = 1;
    else if (strcmp(buf, "set region") == 0) {
      int ret = get_int_input();
      CHK_ERR_JUMP((ret < 0) || (ret >= srvr->nregions), donext,
		   "invalid region %d\n", ret);
      regid = ret;
    }
    else if (strcmp(buf, "set start") == 0) {
      int ret = get_int_input();
      CHK_ERR_JUMP((ret < 0), donext, "invalid start byte %d\n", ret);
      pstart = ret;
    }
    else if (strcmp(buf, "set bytes") == 0) {
      int ret = get_int_input();
      CHK_ERR_JUMP((ret < 0), donext, "invalid start byte %d\n", ret);
      pbytes = ret;
    }
    else if (strcmp(buf, "print") == 0) {
      print_region_bytes(reg, pstart, pbytes);
    }
  donext:
    ;
  }

 alldone:
  printf ("---------------------------------------------\n\n");
}

static inline void
destroy_workers_range(local_state_t *loc, int start, int end)
{
  //TODO: flush and error check
  for (int idx = start; idx < end; idx++) {
    ucp_worker_destroy(loc->workers[idx]);
  }
}

static inline void
destroy_workers(local_state_t *loc)
{
  destroy_workers_range(loc, 0, loc->nworkers);
}


int make_workers(local_state_t *loc)
{
  ucp_worker_params_t worker_params;
  
#ifndef ENABLE_MULTIPLE_WORKERS
  if (loc->nworkers != 1) {
    fprintf (stderr, "Cannot make more than one worker right now, %d != 1\n", loc->nworkers);
    goto err;
  }
#endif
  
  // allocate workers
  loc->workers = (ucp_worker_h*)malloc(loc->nworkers * sizeof(ucp_worker_h));

  if (loc->workers == NULL) {
    fprintf(stderr, "Error allocating ucp_worker_h array of size %d\n", loc->nworkers);
    goto err;
  }
  
  memset(&worker_params, 0, sizeof(worker_params));

  worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
  worker_params.thread_mode = UCS_THREAD_MODE_MULTI;

  for (int idx = 0; idx < loc->nworkers; idx++) {
    if (ucp_worker_create(loc->context, &worker_params, &(loc->workers[idx])) != UCS_OK) {
      printf ("Failed to make worker %d of %d\n", idx, loc->nworkers);
      destroy_workers_range(loc, 0, idx);
      goto err_workers_arr;
    }
  }

  return loc->nworkers;
  
  //err_workers:
  //destroy_workers(loc);

 err_workers_arr:
  free(loc->workers);
 err:
  return -1;
}

void destroy_workers_state(local_state_t *loc)
{
  destroy_workers(loc);
  free(loc->workers);
}



wt_state_t* wt_state_get_new(local_state_t *loc)
{
  wt_state_t *ret =(wt_state_t*)malloc(sizeof(wt_state_t));

  ret->status = wt_state_new;
  ret->server = &(loc->server);
  ret->conn = NULL;

  pthread_mutex_lock(&(loc->state_ls_lock));
  
  if (loc->no_new_wt == 1) {
    pthread_mutex_unlock(&(loc->state_ls_lock));
    free(ret);
    return NULL;
  }

  ret->prev = NULL;
  ret->next = loc->wts;
  if (ret->next != NULL) {
    ret->next->prev = ret;
  }
  loc->wts = ret;
  loc->nwt++;

  pthread_mutex_unlock(&(loc->state_ls_lock));
  return ret;
}

void wt_state_release(local_state_t *loc, volatile wt_state_t *wt)
{
  pthread_mutex_lock(&(loc->state_ls_lock));

  if (wt == loc->wts) {
    loc->wts = wt->next;
  }
  if (wt->next != NULL) {
    wt->next->prev = wt->prev;
  }
  if (wt->prev != NULL) {
    wt->prev->next = wt->next;
  }

  pthread_mutex_unlock(&(loc->state_ls_lock));

  free((void*)wt);
}

void halt_destroy_threads(local_state_t *loc)
{
  pthread_mutex_lock(&(loc->state_ls_lock));
  // Don't allow any more new worker thread states to be added to the list
  loc->no_new_wt = 1;
  pthread_mutex_unlock(&(loc->state_ls_lock));
  
  pthread_mutex_lock(&(loc->wt_status_lock));
  for (volatile wt_state_t *wt = loc->wts; wt != NULL; wt = wt->next) {
    if (wt->status == wt_state_running) {
      // halt any running threads
      wt->status = wt_state_halting;
    }
    else {
      // set any nonrunning threads into error state
      // will prevent any new pthread_create calls
      wt->status = wt_state_err;
    }
  }
  pthread_mutex_unlock(&(loc->wt_status_lock));

  for (volatile wt_state_t *wt = loc->wts; wt != NULL; wt = wt->next) {
    void *retval;
    if (wt->status == wt_state_halting)
      pthread_join(wt->pth, &retval);
  }
  
  while (loc->wts != NULL) {
    wt_state_release(loc, loc->wts);
  }
}

int wt_pthread_create(local_state_t *loc, wt_state_t *wt, const pthread_attr_t *attr,
		      void *(*start_routine) (void *), void *arg)
{
  int ret = -1;
  
  pthread_mutex_lock(&(loc->wt_status_lock));
  if (wt->status != wt_state_new) {
    goto unlock_status;
  }

  wt->status = wt_state_running;
  ret = pthread_create(&(wt->pth), attr, start_routine, arg);

 unlock_status:
  pthread_mutex_unlock(&(loc->wt_status_lock));

  return ret;
}

/*
 * shmemio_connectloop calls this function every time we get a new connection
 * Currently does nothing, but could take actions like making new threads, workers, etc
 */
void test_server_connect_callback(shmemio_server_t *srvr, shmemio_conn_t *newconn, void *args)
{
  local_state_t *loc = (local_state_t*)args;

  printf("Test Server: Got connect callback for newconn ep %p with nsfes %d and nregions %d...\n",
	 newconn->ep, newconn->nfpes, newconn->nregions);

  //Something like this might be useful if we wanted to manage connection
  //resources outside the shmemio library.
  
  //wt_state_t *newwt = wt_state_get_new(loc);
  //newwt->server = srvr;
  //newwt->conn = newconn;
  //wt_pthread_create(loc, newwt, NULL, connect_thread, (void*)(newwt));
}



/*
 * In this new thread, run an interactive test loop, then kill the server
 */
void *server_test_thread(void *arg)
{
  wt_state_t *wt = (wt_state_t*)arg;
  shmemio_server_t *srvr = wt->server;
  
  printf ("Test server: Test thread is go\n");
  
  interactive_server_test(srvr);
  
  printf ("Test server: Test thread is done, halting server\n");

  srvr->status = shmemio_server_halting;
  ucp_worker_signal(srvr->worker);
  
  return NULL;
}

/*
 * Create the basic threads the server needs to operate
 */
int test_server_make_threads(local_state_t *loc)
{
  if (loc->daemonize == 0) {
    wt_state_t *newstate = wt_state_get_new(loc);
    wt_pthread_create(loc, newstate, NULL, server_test_thread, (void*)(newstate));
  }
  
  return 0;
}


/*
 * Init ucx. Create workers.
 */
int test_server_init(local_state_t *loc)
{
  ucp_params_t ucp_params;
  ucp_config_t *config;
  ucs_status_t status;

  loc->no_new_wt = 0;
  loc->nwt = 0;
  loc->wts = NULL;
  pthread_mutex_init(&(loc->wt_status_lock), NULL);
  pthread_mutex_init(&(loc->state_ls_lock), NULL);
  
  memset(&ucp_params, 0, sizeof(ucp_params));
  
  status = ucp_config_read(NULL, NULL, &config);
  CHKERR_JUMP(status != UCS_OK, "ucp_config_read\n", err);

  ucp_params.field_mask   = ( UCP_PARAM_FIELD_FEATURES     |
			      UCP_PARAM_FIELD_MT_WORKERS_SHARED |
                              UCP_PARAM_FIELD_REQUEST_SIZE |
                              UCP_PARAM_FIELD_REQUEST_INIT );

  ucp_params.features     = (UCP_FEATURE_STREAM   |
			     UCP_FEATURE_WAKEUP   |
			     UCP_FEATURE_RMA      |
			     UCP_FEATURE_AMO32    |
			     UCP_FEATURE_AMO64 );

  ucp_params.request_size = sizeof(shmemio_streamreq_t);
  ucp_params.request_init = shmemio_request_init;

  ucp_params.mt_workers_shared = 1;
  
  status = ucp_init(&ucp_params, config, &loc->context);
  CHKERR_JUMP(status != UCS_OK, "ucp_init\n", err);
  
  //debug_log(1);
  
  //ucp_config_print(config, stdout, NULL, UCS_CONFIG_PRINT_CONFIG);
  ucp_config_release(config);

  if (make_workers(loc) != loc->nworkers) {
    printf ("Failed to create %d workers\n", loc->nworkers);
    goto err_cleanup;
  }

  return 0;

  //err_workers:
  //destroy_workers_state(loc);
  
 err_cleanup:
  ucp_cleanup(loc->context);

 err:
  return -1;
  
}

void test_server_shutdown(local_state_t *srvr)
{
  halt_destroy_threads(srvr);
  
  destroy_workers_state(srvr);
  
  ucp_cleanup(srvr->context);
}

int run_server_main()
{
  switch (loc.log_level) {
  case 0:
    shmemio_set_log_level(warn);
    break;
  case 1:
    shmemio_set_log_level(info);
    break;
  case 2:
    shmemio_set_log_level(trace);
    break;
  }
  
  if ( test_server_init(&loc) != 0 ) {
    fprintf (stderr, "Failed to init test server\n");
    goto err;
  }

  printf("Test server: Init shmemio server...\n");
  if (shmemio_init_server(&(loc.server),
			  loc.context, loc.workers[0], loc.nsfpes,
			  &test_server_connect_callback, (void*)&loc,
			  loc.port, loc.sys_pagesize,
			  loc.region_size, loc.default_unit) != 0) {
    fprintf(stderr, "Failed to init shmemio server\n");
    goto err_shutdown;
  }

  if (test_server_make_threads(&loc) != 0) {
    printf ("Failed to create threads\n");
    goto err_shutdown;
  }

  printf("Test server: Initiating server listener...\n");
  if ( shmemio_listen(&(loc.server)) != 0 ) {
    printf ("Failed to initiate server listen\n");
    goto err_destroy;
  }

  printf("Test server: Entering connection loop...\n");
  shmemio_connectloop(&(loc.server));
  
  printf ("Test server: Finalize shmemio server...\n");
  shmemio_finalize_server(&(loc.server));

  printf ("Test server: Shutdown server...\n");
  test_server_shutdown(&loc);

  return 0;

 err_destroy:
  shmemio_finalize_server(&(loc.server));
  
 err_shutdown:
  test_server_shutdown(&loc);
  
 err:
  return -1;
}

int main(int argc, char **argv)
{
  ucs_status_t status;

  loc.sys_pagesize = sys_pagesize;
  loc.default_unit = 1024;
  
  /* Parse the command line */
  status = parse_cmd(argc, argv, &loc);
  if (status != UCS_OK)
    return -1;

  if (loc.daemonize) {
    daemonize();
  }
  
  return run_server_main();
}

#ifdef ENABLE_MUTIPLE_WORKERS
const char cmd_optstr[] = "dn:p:w:s:hvV";
#else
const char cmd_optstr[] = "dn:p:s:hvV";
#endif
  
int parse_cmd(int argc, char * const argv[], local_state_t *loc)
{
  int c = 0, index = 0;
  opterr = 0;

  // Set defaults
  loc->port = 13337;
  loc->region_size = 2 * sys_pagesize;
  loc->nworkers = 1;
  loc->nsfpes = 1;
  loc->log_level = 0;

  loc->daemonize = 0;
  
  while ((c = getopt(argc, argv, cmd_optstr)) != -1) {
    switch (c) {
    case 'd':
      loc->daemonize = 1;
      break;
    case 'p':
      loc->port = atoi(optarg);
      if (loc->port <= 0) {
	fprintf(stderr, "Wrong server port number %d\n", loc->port);
	return UCS_ERR_UNSUPPORTED;
      }
      break;
    case 'v':
      loc->log_level = 1;
      break;
    case 'V':
      loc->log_level = 2;
      break;
#ifdef ENABLE_MUTIPLE_WORKERS
    case 'w':
      loc->nworkers = atoi(optarg);
      if (loc->nworkers <= 0) {
	fprintf(stderr, "Invalid number of workers %d\n", loc->nworkers);
	return UCS_ERR_UNSUPPORTED;
      }
      break;
#endif
    case 'n':
      loc->nsfpes = atoi(optarg);
      if (loc->nsfpes <= 0) {
	fprintf(stderr, "Invalid number of filespaces %d\n", loc->nsfpes);
	return UCS_ERR_UNSUPPORTED;
      }
      break;
    case 's':
      loc->region_size = atoi(optarg);
      if ((loc->region_size % loc->sys_pagesize) != 0) {
	fprintf(stderr, "Invalid region size %u\n", (unsigned)loc->region_size);
	return UCS_ERR_UNSUPPORTED;
      }
    break;
    case '?':
      if (isprint (optopt)) {
	fprintf(stderr, "Unknown option or missing argument `-%c'.\n", optopt);
      } else {
	fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
      }
    case 'h':
    default:
      fprintf(stderr, "Usage: fspace_server [parameters]\n");
      fprintf(stderr, "\nParameters for the Fspace test server are:\n");
      fprintf(stderr, "  -d daemonize the server (default: run interactive)\n");
      fprintf(stderr, "  -n nsfpes Set number of psuedo-fpes. (default:1)\n");
      fprintf(stderr, "  -p port Set server listen port (default:13337)\n");
      fprintf(stderr, "  -v set to verbose (only in debug mode, sets log level=info)\n");
      fprintf(stderr, "  -V set to very verbose (only in debug mode, sets log level=trace)\n");
#ifdef ENABLE_MULTIPLE_WORKERS
      fprintf(stderr, "  -w nworkers Set number of workers. (default:1)\n");
#endif
      fprintf(stderr, "  -s size Set region size. Must be muliple of system pagesize = %z (default:%z)\n",
	    loc->sys_pagesize, loc->sys_pagesize * 2);
      fprintf(stderr, "\n");
      return UCS_ERR_UNSUPPORTED;
    }
  }
  
  for (index = optind; index < argc; index++) {
    fprintf(stderr, "WARNING: Non-option argument %s\n", argv[index]);
  }
  return UCS_OK;
}

