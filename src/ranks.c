/* For license: see LICENSE file at top-level */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif /* HAVE_CONFIG_H */

#include "shmemc.h"
#include "shmemu.h"

#ifdef ENABLE_PSHMEM
#pragma weak shmem_my_pe = pshmem_my_pe
#define shmem_my_pe pshmem_my_pe
#pragma weak shmem_n_pes = pshmem_n_pes
#define shmem_n_pes pshmem_n_pes
#endif /* ENABLE_PSHMEM */

int
shmem_my_pe(void)
{
    int my;

    SHMEMU_CHECK_INIT();

    my = shmemc_my_pe();

    logger(LOG_RANKS, "%s() -> %d", __func__, my);

    return my;
}

int
shmem_n_pes(void)
{
    int n;

    SHMEMU_CHECK_INIT();

    n = shmemc_n_pes();

    logger(LOG_RANKS, "%s() -> %d", __func__, n);

    return n;
}

#ifdef ENABLE_PSHMEM
#pragma weak _my_pe = p_my_pe
#define _my_pe p_my_pe
#pragma weak _num_pes = p_num_pes
#define _num_pes p_num_pes
#endif /* ENABLE_PSHMEM */

int
_my_pe(void)
{
    deprecate(__func__, 1, 2);
    return shmemc_my_pe();
}

int
_num_pes(void)
{
    deprecate(__func__, 1, 2);
    return shmemc_n_pes();
}
