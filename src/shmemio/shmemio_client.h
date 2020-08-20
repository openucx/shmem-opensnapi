/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

/******************************************************************************/

/* client_connect.c */

int shmemio_connect_fspace(shmem_fspace_conx_t *conx, int fid);

int shmemio_client_fopen(shmemio_fspace_t *fio, const char *file, shmemio_fp_t *fp, int *err);

void update_fp_status(shmemio_req_t *req, shmemio_fp_req_t *fpreq, shmem_fp_t *infp);

/******************************************************************************/

/* client_fspace.c */

int shmemio_release_fspace(int fid);

int shmemio_get_new_fspace();

int shmemio_fill_regions(shmemio_fspace_t* fio, int start, int rmax);

int shmemio_fill_fspace(int fid);









