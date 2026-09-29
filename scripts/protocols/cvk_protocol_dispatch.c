/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Workgroup coverage shared by compute and mesh-task dispatch compositions. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Derives the number of workgroups needed to cover one dimension of items.
 *
 * \par Details:
 *  Both factors are widened before multiplication. The largest rounded numerator
 *  is UINT32_MAX squared plus UINT32_MAX minus one, which fits uint64_t. The
 *  quotient never exceeds itemCount. This arithmetic query does not validate
 *  physical-device dispatch limits; callers must query those before dispatch.
 *
 * \par Requirements:
 *  - 001 Zero items per invocation or zero local size shall assert as caller misuse.
 *  - 002 Zero items shall yield zero groups; partial final groups shall round upward.
 *  - 003 Every uint32_t item count and positive factor pair shall be representable.
 *  - 004 No allocation, native operation or device state shall be required.
 *
 * \param[in] itemCount          Items to cover on this axis
 * \param[in] itemsPerInvocation Items processed by one invocation on this axis
 * \param[in] localSize          Invocations per workgroup on this axis
 *
 * \retval    0..itemCount   Required workgroups, including any partial final group.
 */
uint32_t cvkGetWorkgroupCount(uint32_t itemCount, uint32_t itemsPerInvocation, uint32_t localSize)
{
    uint64_t items_per_group;
    uint64_t rounded_count;
    uint32_t group_count;

    CVK_ASSERT(0 < itemsPerInvocation);
    CVK_ASSERT(0 < localSize);
    items_per_group = (uint64_t)itemsPerInvocation * (uint64_t)localSize;
    rounded_count   = (uint64_t)itemCount + items_per_group - 1;
    group_count     = (uint32_t)(rounded_count / items_per_group);
    return group_count;
}
