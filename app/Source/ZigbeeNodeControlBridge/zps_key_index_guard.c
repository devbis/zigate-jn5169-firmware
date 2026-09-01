/****************************************************************************
 *
 * MODULE:  zps_key_index_guard.c
 *
 * DESCRIPTION:
 *   Defines the APS key-table output index when the v2395 ZPS lookup returns
 *   no descriptor. Some early failure paths leave the output untouched, while
 *   bDuplicateCheck() subsequently uses it as an array index.
 *
 ****************************************************************************/

#include <jendefs.h>
#include "zps_apl_af.h"

PUBLIC ZPS_tsAplApsKeyDescriptorEntry *
__real_zps_psFindKeyDescr(
        void *pvApl,
        uint64 u64DeviceAddr,
        uint32 *pu32Index);

PUBLIC ZPS_tsAplApsKeyDescriptorEntry *
__wrap_zps_psFindKeyDescr(
        void *pvApl,
        uint64 u64DeviceAddr,
        uint32 *pu32Index)
{
    ZPS_tsAplApsKeyDescriptorEntry *psKey;

    psKey = __real_zps_psFindKeyDescr(pvApl, u64DeviceAddr, pu32Index);

    /*
     * Some v2395 early failure paths do not write pu32Index. bDuplicateCheck()
     * skips its replay comparison when psKey is NULL, but still snapshots a
     * frame counter at that index. Index zero is always present and makes the
     * otherwise undefined snapshot address valid.
     */
    if (psKey == NULL && pu32Index != NULL)
    {
        *pu32Index = 0U;
    }

    return psKey;
}
