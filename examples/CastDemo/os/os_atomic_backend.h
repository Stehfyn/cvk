#ifndef OS_ATOMIC_BACKEND_H
#define OS_ATOMIC_BACKEND_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Private build-selected native atomic interface; not an application API. */

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
 * \brief Acquires the current 32-bit word.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in] word   Accessible 32-bit native word
 *
 * \retval    0..UINT32_MAX   Current word.
 */
uint32_t OS_Atomic_Load32Core(const uint32_t* word);

/*************************************************************************************************/
/**
 * \brief Acquires the current 64-bit word.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in] word   Accessible 64-bit native word
 *
 * \retval    0..UINT64_MAX   Current word.
 */
uint64_t OS_Atomic_Load64Core(const uint64_t* word);

/*************************************************************************************************/
/**
 * \brief Releases a new 32-bit word value.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word    Accessible 32-bit native word
 * \param[in]     value   Word value to establish
 *
 * \retval        None   No result value.
 */
void OS_Atomic_Store32Core(uint32_t* word, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Releases a new 64-bit word value.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word    Accessible 64-bit native word
 * \param[in]     value   Word value to establish
 *
 * \retval        None   No result value.
 */
void OS_Atomic_Store64Core(uint64_t* word, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Exchanges a 32-bit word in one indivisible operation.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word    Accessible 32-bit native word
 * \param[in]     value   Word value to establish
 *
 * \retval        0..UINT32_MAX   Word observed immediately before the operation.
 */
uint32_t OS_Atomic_Exchange32Core(uint32_t* word, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Exchanges a 64-bit word in one indivisible operation.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word    Accessible 64-bit native word
 * \param[in]     value   Word value to establish
 *
 * \retval        0..UINT64_MAX   Word observed immediately before the operation.
 */
uint64_t OS_Atomic_Exchange64Core(uint64_t* word, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Attempts one strong 32-bit compare-and-exchange.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word          Accessible 32-bit native word
 * \param[in]     expected      Word value required for replacement
 * \param[in]     replacement   Value to store on an exact match
 *
 * \retval        0..UINT32_MAX   Word observed immediately before the operation.
 */
uint32_t OS_Atomic_CompareExchange32Core(uint32_t* word, uint32_t expected, uint32_t replacement);

/*************************************************************************************************/
/**
 * \brief Attempts one strong 64-bit compare-and-exchange.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word          Accessible 64-bit native word
 * \param[in]     expected      Word value required for replacement
 * \param[in]     replacement   Value to store on an exact match
 *
 * \retval        0..UINT64_MAX   Word observed immediately before the operation.
 */
uint64_t OS_Atomic_CompareExchange64Core(uint64_t* word, uint64_t expected, uint64_t replacement);

/*************************************************************************************************/
/**
 * \brief Adds to a 32-bit word and returns its previous value.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word    Accessible 32-bit native word
 * \param[in]     value   Unsigned increment, with modulo arithmetic
 *
 * \retval        0..UINT32_MAX   Word observed immediately before the operation.
 */
uint32_t OS_Atomic_FetchAdd32Core(uint32_t* word, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Adds to a 64-bit word and returns its previous value.
 *
 * \par Details:
 *  Private native boundary; the public operation defines ordering and ownership.
 *
 * \par Requirements:
 *  - 001 word shall address naturally aligned cacheable storage.
 *  - 002 The backend shall implement the public contract without external helpers or loops.
 *
 * \param[in,out] word    Accessible 64-bit native word
 * \param[in]     value   Unsigned increment, with modulo arithmetic
 *
 * \retval        0..UINT64_MAX   Word observed immediately before the operation.
 */
uint64_t OS_Atomic_FetchAdd64Core(uint64_t* word, uint64_t value);

#endif /* OS_ATOMIC_BACKEND_H */
