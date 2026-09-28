#ifndef CAST_MP4_INDEX_H
#define CAST_MP4_INDEX_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ISO/IEC 14496-12:2012, 8.6.1.2 and 8.6.2: decoding-time and sync-sample tables.
 * Native MP4/MOV file adapter; platform/encoding abstraction remains incomplete.
 * Composition offsets and movie edits are not mapped by this decode-time index.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <wchar.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define MP4_INDEX_MAX_KEYFRAMES (65536) ///< Startup-owned application timestamp capacity.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Reads a complete bounded video sync-sample index in media decode-time seconds.
 *
 * \par Requirements:
 *  - 001 The native file path shall be present; nonzero capacity shall require disjoint writable timestamp storage.
 *  - 002 The output is scratch until a nonzero count is returned; failures may leave unpublished scratch changes.
 *  - 003 No table heap storage shall be allocated; two fixed pages shall stream declared table entries.
 *  - 004 Invalid ordering, truncation, extent errors and cumulative timeline overflow shall reject the entire index.
 *  - 005 Absent stss means every sample is sync; absent, empty or oversized indexes shall return zero for exact-seek fallback.
 *  - 006 Zero capacity shall return zero without opening or reading the file.
 *  - 007 File opening and synchronous I/O remain native operations without proven realtime or allocation-free lifecycle guarantees.
 *
 * \param[in] pFile   Terminated native wide-character path
 * \param[out] pTimes   Scratch timestamp array, required for nonzero capacity
 * \param[in] capacity   Writable timestamp slots
 *
 * \retval nonzero Complete index count, no greater than capacity.
 * \retval 0 No retained index; leave the requested seek time unchanged.
 */
unsigned Mp4Index_ReadKeyframes(const wchar_t* pFile, double* pTimes, unsigned capacity);

/*************************************************************************************************/
/**
 * \brief Selects the last indexed decode time at or before a requested media time.
 *
 * \par Requirements:
 *  - 001 A nonempty array shall contain a complete valid index in nondecreasing order.
 *  - 002 An empty index or a request before its first entry shall preserve the request.
 *  - 003 The caller shall supply a finite request on the same media decode timeline.
 *
 * \param[in] pTimes   Stable complete index, nullable only when count is zero
 * \param[in] count   Readable timestamp entries
 * \param[in] seconds   Finite requested decode-time position
 *
 * \retval seconds No qualifying indexed time.
 * \retval timestamp Last qualifying index entry.
 */
double Mp4Index_SnapToKeyframe(const double* pTimes, unsigned count, double seconds);

#endif /* CAST_MP4_INDEX_H */
