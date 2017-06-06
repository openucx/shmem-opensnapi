#include "comms.h"
#include "pe.h"

#include <unistd.h>
#include <string.h>

#include <ucp/api/ucp.h>

#define KB 1024L
#define MB (KB * KB)
#define GB (KB * MB)

/*
 * this should probably go into the pe struct below
 */
endpoint_t *endpoints;

/*
 * the PE's state
 */
pe_info_t proc = {
    .status = SHMEM_PE_UNKNOWN,
    .refcount = 0
};

/*
 * private shortcut to communications structure
 */
static comms_info_t *c_p = & proc.comms;

/*
 * shortcut to look up the UCP endpoint
 */
#define LOOKUP_UCP_EP(pe) (endpoints[(pe)].remote_ep)

/*
 * where globals/statics/commons/saves live
 *
 */
static ucp_mem_h global_segment;

/*
 * some memory to play with registering
 *
 * TODO: multiple heap support
 */
static ucp_mem_h symm_heap;

/*
 * debugging output stream
 */
static FILE *say;

#if 0
/*
 * TODO: move to shmemu namespace in real implementation
 */
static void *
shmemu_round_down_address_to_pagesize(void *addr)
{
    const long psz = sysconf(_SC_PAGESIZE);
    /* make sure aligned to page size multiples */
    const size_t mod = (size_t) addr % psz;

    if (mod != 0) {
        const size_t div = (size_t) addr / psz;

        addr = (void *) ((div - 1) * psz);
    }

    return addr;
}
#endif

char *
shmemc_getenv(const char *name)
{
    return getenv(name);
}

static void
check_version(void)
{
    unsigned int maj, min, rel;

    ucp_get_version(&maj, &min, &rel);

    fprintf(say, "Piecewise query:\n");
    fprintf(say, "    UCX version \"%u.%u.%u\"\n", maj, min, rel);
    fprintf(say, "String query\n");
    fprintf(say, "    UCX version \"%s\"\n", ucp_get_version_string());
    fprintf(say, "\n");
}

static void
make_worker(void)
{
    ucs_status_t s;
    ucp_worker_params_t wkpm;

    wkpm.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    wkpm.thread_mode = UCS_THREAD_MODE_SINGLE;

    s = ucp_worker_create(c_p->ctxt, &wkpm, &c_p->wrkr);
    assert(s == UCS_OK);
}

static void
make_init_params(ucp_params_t *p_p)
{
    p_p->field_mask =
        UCP_PARAM_FIELD_FEATURES |
        UCP_PARAM_FIELD_ESTIMATED_NUM_EPS;

    p_p->estimated_num_eps = proc.nranks;

    /*
     * we'll try at first to get both 32- and 64-bit direct AMO
     * support because OpenSHMEM wants them
     */
    p_p->features =
        UCP_FEATURE_WAKEUP |
        UCP_FEATURE_RMA
#if 1
    /* while testing so we can play on mlx4 card */
    |
        UCP_FEATURE_AMO32 |
        UCP_FEATURE_AMO64
#endif
        ;
}

static void
make_local_endpoint(void)
{
    ucs_status_t s;
    ucp_ep_params_t epm;

    epm.field_mask = UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
    epm.address = proc.comms.addr_p;

    s = ucp_ep_create(c_p->wrkr, &epm, &c_p->my_ep);
    assert(s == UCS_OK);
}

/*
 * create remote endpoint table
 */
static void
make_remote_endpoints(void)
{
    endpoints = (endpoint_t *) calloc(proc.nranks, sizeof(*endpoints));
    assert(endpoints != NULL);
}

/*
 * private helper
 */
inline static void
do_flush(void)
{
    ucs_status_t s;

    s = ucp_worker_flush(c_p->wrkr);
    assert(s == UCS_OK);
}

static void
dump_mapped_mem_info(const char *name, const ucp_mem_h *m)
{
    ucs_status_t s;
    ucp_mem_attr_t attr;

    /* the attributes we want to inspect */
    attr.field_mask =
        UCP_MEM_ATTR_FIELD_ADDRESS |
        UCP_MEM_ATTR_FIELD_LENGTH;

    s = ucp_mem_query(*m, &attr);
    assert(s == UCS_OK);

    fprintf(say,
            "%d: \"%s\" memory at %p, length %.2f MB (%lu bytes)\n",
            proc.rank, name,
            attr.address,
            (double) attr.length / MB,
            (unsigned long) attr.length
            );
}

extern int shmemu_parse_size(char *size_str, size_t *bytes_p);

static void
reg_symmetric_heap(void)
{
    ucs_status_t s;
    ucp_mem_map_params_t mp;
    char *hs_str = shmemc_getenv("SHMEM_SYMMETRIC_HEAP_SIZE");
    size_t len;

    if (hs_str != NULL) {
        const int r = shmemu_parse_size(hs_str, &len);
        assert(r == 0);
    }
    else {
        len = 4 * MB; /* just some usable value */
    }

    mp.field_mask =
        UCP_MEM_MAP_PARAM_FIELD_LENGTH |
        UCP_MEM_MAP_PARAM_FIELD_FLAGS;

    mp.length = len;
    mp.flags =
        UCP_MEM_MAP_ALLOCATE;

    s = ucp_mem_map(c_p->ctxt, &mp, &symm_heap);
    assert(s == UCS_OK);
}

extern char data_start;
extern char end;

static void
reg_globals(void)
{
    ucs_status_t s;
    ucp_mem_map_params_t mp;
    void *base = &data_start;
    const size_t len = (size_t) &end - (size_t) base;

    mp.field_mask =
        UCP_MEM_MAP_PARAM_FIELD_ADDRESS |
        UCP_MEM_MAP_PARAM_FIELD_LENGTH;

    mp.address = base;
    mp.length = len;

    s = ucp_mem_map(c_p->ctxt, &mp, &global_segment);
    assert(s == UCS_OK);
}

/**
 * API
 *
 **/

void
shmemc_quiet(void)
{
    do_flush();
}

void
shmemc_init(void)
{
    ucs_status_t s;
    ucp_params_t pm;

    say = stderr;

    proc.refcount += 1;

    /* no re-init */
    if (proc.status == SHMEM_PE_UP) {
        return;
    }

    /* start initialization */
    s = ucp_config_read(NULL, NULL, &c_p->cfg);
    assert(s == UCS_OK);

    make_init_params(&pm);

    s = ucp_init(&pm, c_p->cfg, &c_p->ctxt);
    assert(s == UCS_OK);

    make_worker();

    /* get address for remote access to worker */
    s = ucp_worker_get_address(c_p->wrkr, &c_p->addr_p, &c_p->addr_len);
    assert(s == UCS_OK);

    /*
     * try registering some implicit memory as symmetric heap
     *
     */
    reg_symmetric_heap();

    /*
     * try registering the data and bss segments
     *
     * ok, this is wiping out global variables.  What's the
     * alernative?
     */
    reg_globals();

    make_local_endpoint();
    make_remote_endpoints();

#ifdef DEBUG
    if (proc.rank == 0) {
        const ucs_config_print_flags_t flags =
            UCS_CONFIG_PRINT_CONFIG |
            UCS_CONFIG_PRINT_HEADER;

        ucp_config_print(c_p->cfg, say, "My config", flags);
        ucp_context_print_info(c_p->ctxt, say);
        check_version();
        fprintf(say, "----------------------------------------------\n\n");
        fflush(say);
    }
    ucp_worker_print_info(c_p->wrkr, say);
    dump_mapped_mem_info("heap", &symm_heap);
    dump_mapped_mem_info("globals", &global_segment);
#endif
    /* don't need config info any more */
    ucp_config_release(c_p->cfg);

    proc.status = SHMEM_PE_UP;
}

void
shmemc_finalize(void)
{
    ucs_status_t s;

    proc.refcount -= 1;

    if (proc.status != SHMEM_PE_UP) {
        return;
    }

    /* really want a global barrier */
    shmemc_quiet();

    /* and clean up */
    free(endpoints);

    ucp_disconnect_nb(c_p->my_ep);

    ucp_worker_release_address(c_p->wrkr, c_p->addr_p);

    ucp_worker_destroy(c_p->wrkr);

    s = ucp_mem_unmap(c_p->ctxt, symm_heap);
    assert(s == UCS_OK);
    s = ucp_mem_unmap(c_p->ctxt, global_segment);
    assert(s == UCS_OK);

    ucp_cleanup(c_p->ctxt);

    proc.status = SHMEM_PE_SHUTDOWN;
}

/*
 *
 * puts & gets
 *
 */

void shmemc_put(void *dest, const void *src,
                size_t nbytes, int pe)
{
    uint64_t r_dest = (uint64_t) dest; /* address on other PE */
    ucp_rkey_h rkey;            /* rkey for this remote address */
    ucs_status_t s;

    s = ucp_put(LOOKUP_UCP_EP(pe), src, nbytes, r_dest, rkey);
    assert(s == UCS_OK);
}

void shmemc_get(void *dest, const void *src,
                size_t nbytes, int pe)
{
    uint64_t r_src = (uint64_t) src; /* address on other PE */
    ucp_rkey_h rkey;            /* rkey for this remote address */
    ucs_status_t s;

    s = ucp_get(LOOKUP_UCP_EP(pe), dest, nbytes, r_src, rkey);
    assert(s == UCS_OK);
}

/* TODO: AND SO ON */

/*
 * -- atomics ------------------------------------------------------------
 */

/*
 * helpers
 */

inline static
uint32_t
helper_fadd32(uint32_t *t, uint32_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    uint32_t ret;
    ucs_status_t s;

    s = ucp_atomic_fadd32(LOOKUP_UCP_EP(pe), v, r_t, rkey, &ret);
    assert(s == UCS_OK);

    return ret;
}

inline static
uint64_t
helper_fadd64(uint64_t *t, uint64_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    uint64_t ret;
    ucs_status_t s;

    s = ucp_atomic_fadd64(LOOKUP_UCP_EP(pe), v, r_t, rkey, &ret);
    assert(s == UCS_OK);

    return ret;
}

inline static
void
helper_add32(uint32_t *t, uint32_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    ucs_status_t s;

    s = ucp_atomic_add32(LOOKUP_UCP_EP(pe), v, r_t, rkey);
    assert(s == UCS_OK);
}

inline static
void
helper_add64(uint64_t *t, uint64_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    ucs_status_t s;

    s = ucp_atomic_add64(LOOKUP_UCP_EP(pe), v, r_t, rkey);
    assert(s == UCS_OK);
}

inline static
uint32_t
helper_swap32(uint32_t *t, uint32_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    uint32_t ret;
    ucs_status_t s;

    s = ucp_atomic_swap32(LOOKUP_UCP_EP(pe), v, r_t, rkey, &ret);
    assert(s == UCS_OK);

    return ret;
}

inline static
uint64_t
helper_swap64(uint64_t *t, uint64_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    uint64_t ret;
    ucs_status_t s;

    s = ucp_atomic_swap64(LOOKUP_UCP_EP(pe), v, r_t, rkey, &ret);
    assert(s == UCS_OK);

    return ret;
}

inline static
uint32_t
helper_cswap32(uint32_t *t, uint32_t c, uint32_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    uint32_t ret;
    ucs_status_t s;

    s = ucp_atomic_cswap32(LOOKUP_UCP_EP(pe), c, v, r_t, rkey, &ret);
    assert(s == UCS_OK);

    return ret;
}

inline static
uint64_t
helper_cswap64(uint64_t *t, uint64_t c, uint64_t v, int pe)
{
    uint64_t r_t = (uint64_t) t;
    ucp_rkey_h rkey;
    uint64_t ret;
    ucs_status_t s;

    s = ucp_atomic_cswap64(LOOKUP_UCP_EP(pe), c, v, r_t, rkey, &ret);
    assert(s == UCS_OK);

    return ret;
}

/**
 * API
 **/

/*
 * add
 */
void shmemc_add_int(int *t, int v, int pe)
{
    helper_add32(t, v, pe);
}

void
shmemc_add_long(long *t, long v, int pe)
{
    helper_add64(t, v, pe);
}

void
shmemc_add_longlong(long long *t, long long v, int pe)
{
    helper_add64(t, v, pe);
}

/*
 * inc is just "add 1"
 */
void
shmemc_inc_int(int *t, int pe)
{
    helper_add32(t, 1, pe);
}

void
shmemc_inc_long(long *t, int pe)
{
    helper_add64(t, 1, pe);
}

void
shmemc_inc_longlong(long long *t, int pe)
{
    helper_add64(t, 1, pe);
}

/*
 * fetch-and-add
 */
int
shmemc_fadd_int(int *t, int v, int pe)
{
    return helper_fadd32(t, v, pe);
}

long
shmemc_fadd_long(long *t, long v, int pe)
{
    return helper_fadd64(t, v, pe);
}

long long
shmemc_fadd_longlong(long long *t, long long v, int pe)
{
    return helper_fadd64(t, v, pe);
}

/*
 * finc is just "fadd 1"
 */

int
shmemc_finc_int(int *t, int pe)
{
    return helper_fadd32(t, 1, pe);
}

long
shmemc_finc_long(long *t, int pe)
{
    return helper_fadd64(t, 1, pe);
}

long long
shmemc_finc_longlong(long *t, int pe)
{
    return helper_fadd64(t, 1, pe);
}

/*
 * swaps
 */

int
shmemc_swap_int(int *t, int v, int pe)
{
    return helper_swap32(t, v, pe);
}

long
shmemc_swap_long(long *t, long v, int pe)
{
    return helper_swap64(t, v, pe);
}

long long
shmemc_swap_longlong(long long *t, long long v, int pe)
{
    return helper_swap64(t, v, pe);
}

int
shmemc_cswap_int(int *t, int c, int v, int pe)
{
    return helper_cswap32(t, c, v, pe);
}

long
shmemc_cswap_long(long *t, long c, long v, int pe)
{
    return helper_cswap64(t, c, v, pe);
}

long long
shmemc_cswap_longlong(long long *t, long long c, long long v, int pe)
{
    return helper_cswap64(t, c, v, pe);
}

/*
 * fetch & set
 *
 * TODO: silly impl. for now
 *
 */

int
shmemc_fetch_int(int *t, int pe)
{
    return helper_fadd32(t, 0, pe);
}

long
shmemc_fetch_long(long *t, int pe)
{
    return helper_fadd64(t, 0, pe);
}

long long
shmemc_fetch_longlong(long long *t, int pe)
{
    return helper_fadd64(t, 0, pe);
}

void
shmemc_set_int(int *t, int v, int pe)
{
    *t = v;
}

void
shmemc_set_long(long *t, long v, int pe)
{
    *t = v;
}

void
shmemc_set_longlong(long long *t, long long v, int pe)
{
    *t = v;
}