/* For license: see LICENSE file at top-level */
// Copyright (c) 2018 - 2020 Arm, Ltd

#include "shmemio.h"
#include "shmemio_server.h"

#include "shmemio_test_util.h"

#include <sys/types.h> /* open */
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/mman.h>  /* mmap */

#include <sys/stat.h> /* stat */
#include <unistd.h>

#include <ctype.h>

//Use this to switch file directories for fspace mapped files
//This will switch from pmem to regular memory
//TODO: make part of server config/launch
#define FSPACE_DIR "/mnt/pmemd/tmp/fspace-data"

//Define this to define fake MAP_SHARED_VALIDATE and MAP_SYNC for mapping pmem
//#define HACK_MAP_SHARED_VALIDATE

//Quick check file existance
//#define access_fileok(_path_) (access(_path_,F_OK)==0)

static int
open_pmem_partfile(const char* fname, size_t length)
{
  int fd = open(fname, O_RDWR);

  if (fd >= 0) {
    //The partfile already exists. No need to create new one
    //TODO: size check and extend if needed
    return fd;
  }
  
  fd = open(fname, O_RDWR | O_CREAT, 0644);
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

// Can use this to test experimental MAP_SHARED_VALIDATE support
// Is in kernel. Is not in glibc headers until 2.8+
#ifdef HACK_MAP_SHARED_VALIDATE

#ifndef MAP_SYNC
#define MAP_SYNC	0x80000
#endif
#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE	0x03
#endif

#endif //HACK_MAP_SHARED_VALIDATE

static void *
mmap_pmem_file(char *partfile, size_t length)
{
  shmemio_log(info, "Attempting to map file: %s\n", partfile);
  
  int fd = open_pmem_partfile(partfile, length);
  
  if (fd < 0) {
    shmemio_log(error, "partfile create failed: %s\n", partfile);
    goto err;
  }

#ifndef USE_MAP_SHARED_VALIDATE
  void *addr = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

#else  
  void *addr = mmap(NULL, length, PROT_READ | PROT_WRITE,
  		    MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0);
#endif
  
  shmemio_log(info,
	      "memory mapping file at fd %d result in addr %p\n",
	      fd, addr);
  
#if 0
  //POSIX says we can close the file after we mem map it...
  //Do we trust this for rdma/pmem?
  close(fd);
#endif
  
  return addr;
  
 err:
  return MAP_FAILED;
}

static inline int
create_dir (const char *dir)
{
  struct stat st = {0};

  if (stat(dir, &st) == -1) {
    return mkdir(dir, 0700);
  }

  return 0;
}

static inline void
canonicalize( char *buf, size_t len) {
  while ((*buf != '\0') && (len > 0)) {
    if (!isalnum(*buf) && (*buf != '-') &&
	(*buf != '_') && (*buf != '.') )
      {
	*buf = '_';
      }
    buf++;
    len--;
  }
}

static inline char*
pmem_partfile_pathn(char *buf, size_t size, const char *sfile_key, int partid)
{
  //TODO: make file dir part of server config
  const char pmem_dir[] = FSPACE_DIR;

  if (create_dir(pmem_dir) != 0) {
    shmemio_log(error, "directory create failed: %s\n", pmem_dir);
  }
  
  if (snprintf(buf, size, "%s/parts-%s..part-%d",
	       pmem_dir, sfile_key, partid) >= size) {
    //TODO: in general, fix file part names to fixed length string
    shmemio_log(warn, "directory name truncated: %s\n", buf);
  }

  canonicalize(buf + strlen(pmem_dir) + 1, strlen(buf));
  
  return buf;
}

static inline int
shmemio_free_ucp_pmem(ucp_context_h context, ucp_mem_h mem_handle)
{
  ucs_status_t s;
  
  s = ucp_mem_unmap(context, mem_handle);
  if (s != UCS_OK) {
    return -1;
  }
  return 0;
}

static inline int
shmemio_map_ucp_pmem(ucp_context_h context, size_t length,
		     const char *sfile_key, int partid,
		     ucp_mem_h *mem_handle)
{
  ucs_status_t s;
  ucp_mem_map_params_t mp;
  void *addr = NULL;
  char path_buf[2048];
  char *partfile_name = pmem_partfile_pathn(path_buf, 2048, sfile_key, partid);

  addr = mmap_pmem_file(partfile_name, length);
  if (addr == MAP_FAILED) {
    shmemio_log(error, "pmem file mapping failed\n");
    goto err;
  }

  if (0) {
    mp.field_mask =
      UCP_MEM_MAP_PARAM_FIELD_LENGTH |
      UCP_MEM_MAP_PARAM_FIELD_FLAGS;
    
    mp.flags = UCP_MEM_MAP_ALLOCATE;
  }
  else {
    mp.field_mask =
      UCP_MEM_MAP_PARAM_FIELD_LENGTH |
      UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
      UCP_MEM_MAP_PARAM_FIELD_FLAGS;
    
    mp.flags = UCP_MEM_MAP_NONBLOCK;
    mp.address             = addr;
  }
    
  mp.length = length;

  shmemio_log(info, "Map allocate memory with ucp to partfile %s, size = %lu\n",
	      partfile_name, (long unsigned)length);

  s = ucp_mem_map(context, &mp, mem_handle);
  if (s != UCS_OK) {
    fprintf (stderr, "can't map symmetric heap memory\n");
    goto err;
  }

  return 0;
  
 err:
  return -1;
}


// Flush this range of addresses to persistent storage.
// Return nonzero if there are some addresses in range that are not in pmem, or flush fails

// getconf LEVEL1_DCACHE_LINESIZE - it is broken in RHEL so we have to define own constant
#define SHMEMIO_CACHELINE 64
int
shmemio_flush_to_persist(const void *addr, size_t len)
{
    uintptr_t c_ptr;
    for (c_ptr = (uintptr_t)addr & ~((uintptr_t)SHMEMIO_CACHELINE - 1);
         c_ptr < (uintptr_t)addr + len; c_ptr +=SHMEMIO_CACHELINE) {
	    // Flush cache line by cacheline 
#if defined(_ARM64_) || defined(__aarch64__) || defined(__ARM_ARCH_ISA_A64)
#if defined(HWCAP_DCPOP)
            // 8.2 and above should have PoP defined
	    asm volatile("dc cvap, %0" : : "r" (c_ptr) : "memory");
#else 
            // For earlier versions we use PoC
	    asm volatile("dc cvac, %0" : : "r" (c_ptr) : "memory");
#endif
#else
#error "ARM64 architecture not detected - No flush instructions in flush to persist"
#endif
	    
    }

}


int
shmemio_release_sfpe_mem(shmemio_sfpe_mem_t *sm, ucp_context_h context)
{
  int ret = 0;
  if (sm->rkey_len > 0) {
    ucp_rkey_buffer_release(sm->packed_rkey);
    sm->rkey_len = 0;
  }

  if (sm->len > 0) {
    if (shmemio_free_ucp_pmem(context, sm->mem_handle) != 0) {
      shmemio_log(error, "can't evict/unmap symmetric heap memory");
      return -1;
    }
    sm->len = 0;
  }

  return 0;
}

/*
  sm        = memory region associated with some server fpe
  context   = server context
  length    = length of memory region for part of file
  sfile_key = key for sfile associated with this new allocated memory
  partid    = new memory per sfpe may be one file across multiple sfpe
              This is the unique partid for some sfile_key split across sfpe
 */
size_t
shmemio_init_sfpe_mem(shmemio_sfpe_mem_t *sm,
		      ucp_context_h context, size_t length,
		      const char *sfile_key, int partid)
{
  
  if (shmemio_map_ucp_pmem(context, length,
			   sfile_key, partid,
			   &(sm->mem_handle)) != 0) {
    shmemio_log(error, "can't malloc rdma accessible pmem\n");
    sm->len = 0;
    return 0;
  }

  sm->attr.field_mask =
    UCP_MEM_ATTR_FIELD_ADDRESS |
    UCP_MEM_ATTR_FIELD_LENGTH;

  ucs_status_t s = ucp_mem_query(sm->mem_handle, &sm->attr);
  if (s != UCS_OK) {
    shmemio_log(error, "can't query extent of symmetric heap memory\n");
    return 0;
  }

  sm->base = (uint64_t) sm->attr.address;
  sm->end  = sm->base + sm->attr.length;
  sm->len  = sm->attr.length;

  s = ucp_rkey_pack(context, sm->mem_handle,
		    &sm->packed_rkey, &sm->rkey_len);

  if (s != UCS_OK) {
    shmemio_log(error, "failed to pack rkey\n");
    sm->rkey_len = 0;
    return 0;
  }

  shmemio_log(trace, "Prepared sfpe memory [%x:%x] len = %d, packed key %p [%lu...], rkley_len %u\n",
	      sm->base, sm->end, sm->len, sm->packed_rkey, *((long unsigned*)sm->packed_rkey), (unsigned)sm->rkey_len);
  
  return sm->len;
}


