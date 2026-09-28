#ifndef UTL_ARGUMENTS_H
#define UTL_ARGUMENTS_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Argument syntax is supplied by the implementation selected by the build.
 * Win32 uses Microsoft C command-line quoting; POSIX preserves exec argument-vector boundaries.
 * Encoding conversion belongs to the OS boundary. Readers contain no OS, compiler-intrinsic,
 * CRT or allocation dependencies. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief One borrowed source record after OS encoding conversion.
 */
typedef struct Utl_Arguments_Record_s
{
    const uint16_t* units;     ///< Non-NULL native-order UTF-16 units; no terminator is counted.
    uint32_t        unitCount; ///< Readable unit count, possibly zero.
} Utl_Arguments_Record_t;

/*************************************************************************************************/
/**
 * \brief Borrowed records representing the target's native argument transport.
 *
 * \par Details:
 *  Win32 supplies one record containing the encoded command line. POSIX supplies
 *  one record for each already-separated argv entry. The OS backend establishes
 *  this model; consumers use the same reader without target-specific branches.
 */
typedef struct Utl_Arguments_s
{
    const Utl_Arguments_Record_t* records;     ///< Borrowed records, non-NULL when recordCount is nonzero.
    uint32_t                      recordCount; ///< One command-line record on Win32; argc records on POSIX.
} Utl_Arguments_t;

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
 * \brief Reads one counted argument using the build-selected target semantics.
 *
 * \par Details:
 *  Input and output contain native-order UTF-16 units. The OS boundary owns encoding
 *  conversion; this reader preserves code units without scalar validation. No shell,
 *  environment or wildcard expansion is performed. No terminator is appended.
 *
 * \par Requirements:
 *  - 001 Required pointers and invalid source-model shape shall assert; all input and output storage shall be disjoint.
 *  - 002 Win32 shall apply program-name, even/odd backslash and doubled-quote rules.
 *  - 003 POSIX shall preserve supplied argv records, including spaces, quotes and empty arguments.
 *  - 004 Missing arguments and insufficient output capacity shall preserve both outputs.
 *  - 005 At most two bounded passes shall occur without allocation, CRT or OS calls.
 *
 * \param[out] unitCount       Non-NULL written-unit count, disjoint from arguments and output
 * \param[out] output          Non-NULL caller-owned units, disjoint from arguments and its records
 * \param[in]  capacity        Writable output units; zero permits an empty argument
 * \param[in]  arguments       Non-NULL model established by the OS backend
 * \param[in]  argumentIndex   Zero-based argument index; zero selects the program name
 *
 * \retval     TRUE    The argument was written, possibly with zero units.
 * \retval     FALSE   The argument is absent or output capacity is insufficient.
 */
bool_t Utl_Arguments_Read(uint32_t*              unitCount,
                          uint16_t*              output,
                          uint32_t               capacity,
                          const Utl_Arguments_t* arguments,
                          uint32_t               argumentIndex);

#endif /* UTL_ARGUMENTS_H */
