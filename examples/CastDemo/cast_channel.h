/* cast_channel.h - a CASTV2 connection: TCP to port 8009, TLS through SChannel, framed
** CastMessages in both directions. Cast receivers present self-signed certificates, so the
** certificate chain is not validated; the channel is only as trusted as the local network. */
#ifndef CAST_CHANNEL_H
#define CAST_CHANNEL_H

#include "utl/utl_castv2.h"
#include "os/os_memory.h"

#define CAST_PORT (8009)

typedef struct CastChannel CastChannel;

typedef struct CastChannel_Workspace_s
{
    OS_Memory_t  memory;  ///< Channel buffers reserved and locked during startup.
    CastChannel* channel; ///< Private reusable channel storage.
} CastChannel_Workspace_t;

/*************************************************************************************************/
/**
 * \brief Establishes reusable channel storage before playback begins.
 *
 * \param[in,out] workspace   Caller-owned startup lifetime
 *
 * \retval TRUE    Storage is resident and ready for repeated connections.
 * \retval FALSE   Storage could not be established; call TryClose to release partial state.
 */
bool_t CastChannel_Initialize(CastChannel_Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Borrows the most recent channel diagnostic.
 *
 * \param[in] pChannel   Retained channel lifetime
 *
 * \retval text   Diagnostic valid until the next channel operation.
 */
const char* CastChannel_GetError(const CastChannel* pChannel);

/*************************************************************************************************/
/**
 * \brief Connects and completes a Cast TLS handshake using startup storage.
 *
 * \par Requirements:
 *  - 001 Initialize and WSAStartup shall precede the connection.
 *  - 002 The previous connection shall be closed before attempting reuse.
 *  - 003 Native cleanup failure shall retain its handles and reject reuse.
 *
 * \param[in,out] workspace       Resident channel lifetime
 * \param[in]     pHost           Receiver host text
 * \param[in]     port            Receiver TCP port
 * \param[out]    pError          Failure text storage
 * \param[in]     errorCapacity   Writable failure text bytes
 *
 * \retval channel   Connected channel borrowed from workspace.
 * \retval NULL      Connection or retained cleanup failed.
 */
CastChannel* CastChannel_Connect(CastChannel_Workspace_t* workspace,
                                 const char*              pHost,
                                 unsigned short           port,
                                 char*                    pError,
                                 size_t                   errorCapacity);

/* Encodes, encrypts and sends one message. Returns 1 on success. */
int CastChannel_Send(CastChannel* pChannel, const Utl_CastV2_Message_t* pMessage);

/* Waits up to timeoutMs for a complete frame. Returns 1 with the message borrowing channel
** storage (valid until the next call), 0 on timeout, -1 when the connection failed or closed. */
int CastChannel_Receive(CastChannel* pChannel, Utl_CastV2_Message_t* pMessage, unsigned timeoutMs);

/*************************************************************************************************/
/**
 * \brief Retries native channel cleanup while retaining its startup storage.
 *
 * \param[in,out] pChannel   Channel returned by Connect; NULL is permitted.
 *
 * \retval None
 */
void CastChannel_Close(CastChannel* pChannel);

/*************************************************************************************************/
/**
 * \brief Releases startup storage after the session runner has closed its channel.
 *
 * \param[in,out] workspace   Quiescent channel lifetime
 *
 * \retval TRUE    The storage has been released.
 * \retval FALSE   Native cleanup remains pending and can be retried.
 */
bool_t CastChannel_TryClose(CastChannel_Workspace_t* workspace);

#endif
