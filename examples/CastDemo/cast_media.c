/*=================================================================================================
** 1. References
**===============================================================================================*/

/** cast_media.h and os_mailbox.h define lifetime and publication contracts. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_media.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_mailbox.h"
#include "os/os_types.h"
#include "utl/utl_unicode.h"
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
 * \par Details:
 *  Unpublished payload is inaccessible, so reset changes only slot ownership.
 */
void CastMedia_Initialize(CastMedia_Channel_t* channel)
{
    LOGASSERT(NULL != channel);

    OS_Mailbox_Initialize(&channel->mailbox);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Bounded scans precede any publication. The reader's slot is never touched,
 *  including when its consumer is suspended during use of a previous pair.
 */
void CastMedia_Publish(CastMedia_Channel_t* channel, const char_t* url, const char_t* type)
{
    uint32_t     url_count;
    uint32_t     type_count;
    bool_t       valid_url;
    bool_t       valid_type;
    uint32_t     index;
    CastMedia_t* media;

    LOGASSERT(NULL != channel);
    LOGASSERT(NULL != url);
    LOGASSERT(NULL != type);

    url_count  = Utl_Unicode_GetUtf8StringByteCount(url, CAST_MEDIA_URL_CAPACITY);
    type_count = Utl_Unicode_GetUtf8StringByteCount(type, CAST_MEDIA_TYPE_CAPACITY);
    valid_url  = (CAST_MEDIA_URL_CAPACITY > url_count);
    valid_type = (CAST_MEDIA_TYPE_CAPACITY > type_count);
    LOGASSERT(valid_url);
    LOGASSERT(valid_type);
    index = OS_Mailbox_GetWriteIndex(&channel->mailbox);
    media = &channel->values[index];
    CRT_Memory_Copy(media->url, url, (uint64_t)url_count + 1);
    CRT_Memory_Copy(media->type, type, (uint64_t)type_count + 1);
    media->urlByteCount  = url_count;
    media->typeByteCount = type_count;
    OS_Mailbox_Publish(&channel->mailbox);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Acquisition transfers one entire typed slot. NULL does not end any existing borrow.
 */
const CastMedia_t* CastMedia_Take(CastMedia_Channel_t* channel)
{
    bool_t             available;
    const CastMedia_t* media;
    uint32_t           index;

    LOGASSERT(NULL != channel);

    available = OS_Mailbox_TryTake(&channel->mailbox);
    media     = NULL;
    if (available)
    {
        index = OS_Mailbox_GetReadIndex(&channel->mailbox);
        media = &channel->values[index];
    }

    return media;
}
