/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#ifndef SHMEMIO_REGION_UTIL_H
#define SHMEMIO_REGION_UTIL_H

static inline void
sort_range_array(size_t *arr, int alen)
{
  size_t s1, s2;
  for (int idx = 0; ((idx+1) < alen); idx += 2) {

    for (int jdx = idx + 2; ((jdx+1) < alen); jdx += 2) {
  
      if (arr[jdx] < arr[idx]) {
	s1 = arr[idx];
	s2 = arr[idx+1];

	arr[idx] = arr[jdx];
	arr[idx+1] = arr[jdx+1];

	arr[jdx] = s1;
	arr[jdx+1] = s2;
      }
    }
  }
}

static inline int
insert_to_sorted_range_array(size_t *arr, int alen, size_t new1, size_t new2)
{
  for (int idx = alen; idx > 1; idx -= 2) {
    if (arr[idx-2] <= new1) {
      arr[idx] = new1;
      arr[idx+1] = new2;
      return alen + 2;
    }

    arr[idx] = arr[idx-2];
    arr[idx+1] = arr[idx-1];
  }

  arr[0] = new1;
  arr[1] = new2;
  return alen + 2;
}

static inline void
insert_fspace_addr_range(shmemio_fspace_t *fio, size_t new1, size_t new2)
{
  if ( (fio->ua_len+2) >= fio->ua_max ) {
    fio->ua_max += 16;
    size_t *arr = (size_t*)realloc(fio->used_addrs, sizeof(size_t) * (fio->ua_max));
    shmemio_assert(arr != NULL, "range array realloc error");
    fio->used_addrs = arr;
  }

  fio->ua_len = insert_to_sorted_range_array(fio->used_addrs, fio->ua_len, new1, new2);

#ifdef ENABLE_DEBUG
  for (int idx = 0; idx < fio->ua_len; idx += 2) {
    shmemio_log(trace, "fspace address range %d: [%lx:%lx]\n", idx,
		(long unsigned)fio->used_addrs[idx], (long unsigned)fio->used_addrs[idx+1]);
  }
#endif
}
/*
 * Find an appropriate sized unused range in the local address regions for this shmem pe
 * This range will be associated with a connected filespace
 */
static inline size_t
find_address_gap(shmemio_fspace_t *fio, size_t len, uint64_t *base)
{
  const size_t min_addr = 0x10000;
  
  if (fio->used_addrs == NULL) {
    fio->ua_max = (proc.comms.nregions + fio->nregions) * 4;
    fio->used_addrs = (size_t*)malloc( fio->ua_max * sizeof(size_t) );

    size_t *ua = fio->used_addrs;
    int adx = 0;
    
    for (int rdx = 0; rdx < proc.comms.nregions; rdx++) {
      mem_info_t *mi = &(proc.comms.regions[rdx].minfo[proc.rank]);
      ua[adx]   = mi->base;
      ua[adx+1] = mi->end;
      adx += 2;
    }

    for (int rdx = 0; rdx < fio->nregions; rdx++) {
      const shmemio_client_region_t *l_reg = &(fio->l_regions[rdx]);
      if (l_reg->l_base != 0) {
	shmemio_assert(l_reg->l_end > l_reg->l_base, "Error in client region range. End before begin.");
	
	ua[adx]    = l_reg->l_base;
	ua[adx+1]  = l_reg->l_end;
	adx += 2;
      }
    }

    fio->ua_len = adx;
    sort_range_array(ua, fio->ua_len);
  }

  for (int idx = fio->ua_len - 2; idx > 0; idx -= 2) {
    //TODO: better overflow detect
    if ( (fio->used_addrs[idx+1] + len) > fio->used_addrs[idx+1] ) {
      *base = fio->used_addrs[idx+1];
      return len;
    }
  }

  return 0;
}


static inline int
remote_region_fill(int fpe, shmemio_remote_region_t *r_reg)
{
  ucs_status_t s;

  shmemio_log(trace, "Filling remote region [%x:%x] len = %d, packed key %p [%lu...], rkley_len %u\n",
	      r_reg->r_base, r_reg->r_end, r_reg->len, r_reg->packed_rkey,
	      *((long unsigned*)r_reg->packed_rkey), (unsigned)r_reg->rkey_len);
  
  s = ucp_ep_rkey_unpack(proc.comms.eps[fpe_to_pe_index(fpe)],
			 r_reg->packed_rkey, &(r_reg->rkey));

  if (s != UCS_OK) {
    shmemio_log(error, "Could not unpack rkey\n");
    r_reg->rkey = NULL;
    return -1;
  }

  shmemio_log(trace, "Unpacked region [%x:%x] rkey %p\n",
	      r_reg->r_base, r_reg->r_end, r_reg->rkey);
  
  return 0;
}

static inline int
remote_region_recv_init(shmemio_remote_region_t *r_reg,
			size_t base, size_t end, size_t len,
			size_t rkey_len)
{
  r_reg->r_base = base;
  r_reg->r_end = end;
  r_reg->len = len;
  r_reg->rkey_len = rkey_len;

  r_reg->rkey = NULL;
  
  r_reg->packed_rkey = malloc(r_reg->rkey_len);
  shmemio_assert(r_reg->packed_rkey != NULL, "packed rkey malloc error");
  
  return 0;
}


static inline void
client_region_release(shmemio_client_region_t* reg)
{
  shmemio_log(info, "Release client region [%x:%x]\n", reg->l_base, reg->l_end);

  if (reg->r_regions != NULL) {
    for (int idx = 0; idx < reg->fpe_size; idx++) {
      shmemio_remote_region_t *rreg = &reg->r_regions[idx];

      shmemio_log(info, "Release remote region %d of %d, packed key %p, rkey %p\n", idx, reg->fpe_size, rreg->packed_rkey, rreg->rkey);
      
      if (rreg->packed_rkey != NULL) {
	free(rreg->packed_rkey);
      }
      if (rreg->rkey != NULL) {
	ucp_rkey_destroy(rreg->rkey);
      }
    }

    free(reg->r_regions);
  }
}

static inline int
client_region_recv_init(shmemio_client_region_t* reg,
			int fpe_start, int fpe_stride, int fpe_size,
			int unit_size)
{
  reg->l_base = 0;
  reg->l_end = 0;
  reg->len = 0;
  
  reg->fpe_start = fpe_start;
  reg->fpe_stride = fpe_stride;
  reg->fpe_size = fpe_size;
  reg->unit_size = unit_size;
  
  reg->r_regions =
    (shmemio_remote_region_t*)malloc(sizeof(shmemio_remote_region_t) * reg->fpe_size);
  shmemio_assert (reg->r_regions != NULL, "remote region malloc error");

  for (int idx = 0; idx < reg->fpe_size; idx++) {
    reg->r_regions[idx].packed_rkey = NULL;
    reg->r_regions[idx].rkey = NULL;
  }

  return 0;
}

static inline int
client_region_fill(shmemio_fspace_t *fio, shmemio_client_region_t *l_reg)
{
  // Size of the new region to add
  size_t len = l_reg->len;
  // Find a gap the right size
  uint64_t base;

  if (find_address_gap(fio, len, &base) != len) {
    shmemio_log(error, "Could not find gap for len %u\n", (unsigned)len);
    return -1;
  }

  l_reg->l_base = base;
  l_reg->l_end = base + len;
  insert_fspace_addr_range(fio, l_reg->l_base, l_reg->l_end);

  shmemio_log(info, "Mapped new client region with addresses [%x:%x], len=%u\n",
	      l_reg->l_base, l_reg->l_end, l_reg->len);

  int fpe = l_reg->fpe_start;
  for (int idx = 0; idx < l_reg->fpe_size; idx++) {
    shmemio_log(trace, "Client [%x:%x] fill remote region %d of %d [fpe %d, pe %d]\n",
		l_reg->l_base, l_reg->l_end, 0, l_reg->fpe_size, fpe, fpe_to_pe_index(fpe));
    
    if (remote_region_fill(fpe, &(l_reg->r_regions[idx])) != 0) {
      shmemio_log(error,"Failed to fill remote region structure for region %d of %d\n", idx, l_reg->fpe_size);
      return -1;
    }
    fpe += l_reg->fpe_stride;
  }

  return 0;
}

static inline void
fspace_release_client_regions(shmemio_fspace_t *fio) {

  if (fio->l_regions != NULL) {
    for (int idx = 0; idx < fio->nregions; idx++) {
      client_region_release(&(fio->l_regions[idx]));
    }

    free(fio->l_regions);
  }
}


static inline int
fspace_extend_client_regions(shmemio_fspace_t *fio, unsigned nnew) {

  int nold = fio->nregions;
  
  if (nold == 0) {
    fio->l_regions =
      (shmemio_client_region_t*)malloc(sizeof(shmemio_client_region_t) * nnew);
    shmemio_log_ret_if(error, -1, (fio->l_regions == NULL), "malloc error");

    fio->nregions = nnew;
  }
  else {
    shmemio_client_region_t* newreg =
      (shmemio_client_region_t*)realloc(fio->l_regions, sizeof(shmemio_client_region_t) * (nnew + nold));

    shmemio_log_ret_if(error, -1, (newreg == NULL), "realloc error");

    fio->l_regions = newreg;
    fio->nregions += nnew;
  }

  shmemio_log(trace, "Extend client regions from %d to %d\n", nold, fio->nregions);
  
  for (int idx = nold; idx < fio->nregions; idx++) {
    fio->l_regions[idx].len = 0;
    fio->l_regions[idx].fpe_size = 0;
    fio->l_regions[idx].r_regions = NULL;
  }

  return nold;
}

#endif
