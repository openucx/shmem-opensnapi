/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

/******************************************************************************/
/* server_connect.c */

/** Export in shmemio.h **/
int shmemio_listen(shmemio_server_t *srvr);

/** Export in shmemio.h **/
int shmemio_connectloop(shmemio_server_t *srvr);


#ifndef SHMEMIO_EXPORT_ONLY
int shmemio_release_all_conns(shmemio_server_t *srvr);

shmemio_conn_t* shmemio_ep_to_conn(shmemio_server_t* srvr, ucp_ep_h ep);

int shmemio_send_response(shmemio_server_t *srvr, ucp_ep_h ep, shmemio_req_t *req, int status);
#endif


/******************************************************************************/
/* server_init.c */

/** Export in shmemio.h **/
int shmemio_init_server(shmemio_server_t *srvr, ucp_context_h context,
			ucp_worker_h worker, int nsfpes,
			shmemio_conn_cb_t cb, void *args,
			uint16_t port, size_t sys_pagesize,
			size_t default_len, size_t default_unit);

/** Export in shmemio.h **/
int shmemio_finalize_server(shmemio_server_t *srvr);


#ifndef SHMEMIO_EXPORT_ONLY
void shmemio_region_mallinfo(mallinfo_t *mi, shmemio_server_region_t* reg);
  
int shmemio_region_malloc(shmemio_server_region_t* reg, size_t size, size_t *offset);

int shmemio_region_realloc(shmemio_server_region_t* reg, size_t size, size_t *offset);

int shmemio_region_realloc_in_place(shmemio_server_region_t* reg, size_t size, size_t *offset);

void shmemio_region_free(shmemio_server_region_t* reg, size_t addr);

int shmemio_new_server_region(shmemio_server_t *srvr, const char *sfile_key,
			      size_t len, int unit_size,
			      int sfpe_start, int sfpe_stride, int sfpe_size);


/******************************************************************************/
/* server_fopen.c */

void shmemio_conn_close_all_files(shmemio_server_t* srvr, shmemio_conn_t *conn);

void shmemio_flush_fspace(shmemio_server_t *srvr, int ioflags);

int shmemio_server_fopen(shmemio_server_t *srvr, ucp_ep_h ep,
			 shmemio_fopen_req_t *foreq,
			 short* status);

int shmemio_try_nonblock_file_act(shmemio_server_t* srvr, int req_type,
				  shmemio_fp_req_t* fpreq, short *status);

int shmemio_try_blocking_file_act(shmemio_server_t* srvr, int req_type,
				  shmemio_fp_req_t* fpreq, short *status);

int shmemio_release_sfile(shmemio_server_t *srvr, shmemio_sfile_t* sfile);

int shmemio_release_all_sfiles(shmemio_server_t *srvr);


/******************************************************************************/
/* server_pmem.c */

int shmemio_flush_to_persist(const void *addr, size_t len);

int shmemio_release_sfpe_mem(shmemio_sfpe_mem_t *sm, ucp_context_h context);

size_t shmemio_init_sfpe_mem(shmemio_sfpe_mem_t *sm,
			     ucp_context_h context, size_t length,
			     const char *sfile_key, int idx);

#endif

#undef SHMEMIO_EXPORT_ONLY
