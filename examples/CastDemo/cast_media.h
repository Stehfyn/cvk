#ifndef CAST_MEDIA_H
#define CAST_MEDIA_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Bounded producer-to-runner media publication for CastDemo. URL/MIME protocol
 * semantics are validated by the consuming specification layers, not this mailbox.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_mailbox.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CAST_MEDIA_URL_CAPACITY  (1200) ///< Application URL storage including termination.
#define CAST_MEDIA_TYPE_CAPACITY (64)   ///< Application MIME storage including termination.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief One coherent URL/MIME pair with explicit payload counts. */
typedef struct CastMedia_s
{
    char_t   url[CAST_MEDIA_URL_CAPACITY];   ///< Terminated media URL.
    char_t   type[CAST_MEDIA_TYPE_CAPACITY]; ///< Terminated MIME type.
    uint32_t urlByteCount;                   ///< Count excluding termination.
    uint32_t typeByteCount;                  ///< Count excluding termination.
} CastMedia_t;

/*************************************************************************************************/
/**
 * \brief Caller-owned channel for one media producer and one runner.
 *
 * \par Details:
 *  Storage is established at application startup. Initialize resets ownership
 *  only while both participants are quiescent. Publish writes only its producer
 *  slot; Take returns a consumer borrow stable until the next successful Take.
 *  Unread publications coalesce to the latest complete pair. No live copy or move.
 */
typedef struct CastMedia_Channel_s
{
    OS_Mailbox_t mailbox;                       ///< Slot ownership metadata.
    CastMedia_t  values[OS_MAILBOX_SLOT_COUNT]; ///< Retained disjoint payloads.
} CastMedia_Channel_t;

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
 * \brief Establishes an empty media channel in retained caller-owned storage.
 *
 * \par Details:
 *  No media publication is pending.
 *
 * \par Requirements:
 *  - 001 Missing required storage shall assert; all previous users shall be quiescent.
 *  - 002 No payload shall be exposed until a complete publication has been acquired.
 *  - 003 No allocation, native resource operation or wait shall occur.
 *
 * \param[out] channel   Required quiescent channel with retained payload storage
 *
 * \retval     None No value is returned.
 */
void CastMedia_Initialize(CastMedia_Channel_t* channel);

/*************************************************************************************************/
/**
 * \brief Publishes a complete URL/MIME pair without waiting for the runner.
 *
 * \par Details:
 *  The pair is published, replacing any unread pair.
 *
 * \par Requirements:
 *  - 001 Only this channel's single producer shall call Publish.
 *  - 002 Required, stable, terminated inputs shall be disjoint from channel storage.
 *  - 003 Each terminator shall fit its fixed capacity; missing termination shall assert as misuse.
 *  - 004 Both text fields and counts shall be complete before ownership publication.
 *  - 005 Work shall be bounded by the two input capacities and one mailbox exchange.
 *
 * \param[in,out] channel   Required initialized producer-side channel
 * \param[in]     url       Required text within CAST_MEDIA_URL_CAPACITY bytes
 * \param[in]     type      Required text within CAST_MEDIA_TYPE_CAPACITY bytes
 *
 * \retval        None No value is returned.
 */
void CastMedia_Publish(CastMedia_Channel_t* channel, const char_t* url, const char_t* type);

/*************************************************************************************************/
/**
 * \brief Acquires the latest pending media pair without copying its payload.
 *
 * \par Requirements:
 *  - 001 Only this channel's single consumer shall call Take.
 *  - 002 Returned storage shall remain borrowed until the next successful Take or quiescent Initialize.
 *  - 003 No pending value shall return NULL without invalidating a previous borrow.
 *  - 004 No allocation, wait, retry, resource operation or payload copy shall occur.
 *
 * \param[in,out] channel   Required initialized consumer-side channel
 *
 * \retval        non-NULL   Coherent counted/terminated pair, owned by the consumer until its next acquisition.
 * \retval        NULL       No pending publication was observed.
 */
const CastMedia_t* CastMedia_Take(CastMedia_Channel_t* channel);

#endif /* CAST_MEDIA_H */
