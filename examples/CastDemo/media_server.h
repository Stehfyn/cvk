#ifndef CAST_MEDIA_SERVER_H
#define CAST_MEDIA_SERVER_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Startup-owned bounded media listeners and shared client workers.
 * HTTP request/framing/response semantics are modeled by cast_http and utl_http.
 * Native socket/file creation and blocking response I/O remain realtime/allocation defects.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "os/os_memory.h"
#include "os/os_thread.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define MEDIA_SERVER_CAPACITY        (32) ///< Simultaneous retained listener representations.
#define MEDIA_SERVER_CLIENT_CAPACITY (8)  ///< Shared startup workers; saturation stays in native backlog.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct MediaServer MediaServer;

/*************************************************************************************************/
/**
 * \brief One caller-owned pool; do not move, copy or mutate a live owner.
 */
typedef struct MediaServer_Workspace_s
{
    OS_Memory_t                   memory;    ///< Resident metadata and fixed client HTTP buffers.
    OS_Thread_t                   admission; ///< Persistent admission/cleanup worker.
    struct MediaServer_Storage_s* storage;   ///< Private storage, borrowed only while resident.
    bool_t                        ready;     ///< Complete startup succeeded and final shutdown has not begun.
    uint32_t                      error;     ///< Retained failure evidence after private storage is released.
} MediaServer_Workspace_t;

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
 * \brief Acquires all listener/client metadata and workers at program initialization.
 *
 * \par Requirements:
 *  - 001 Missing required owner storage shall assert; the caller shall supply fresh or completely closed storage.
 *  - 002 Winsock shall already be initialized and remain available through successful TryClose.
 *  - 003 Every partially acquired native owner shall remain retained for retryable closure.
 *  - 004 Only this operation shall allocate pool storage or create pool execution resources.
 *
 * \param[out] workspace   Stable caller-owned lifetime
 *
 * \retval TRUE    The complete pool and admission worker are ready.
 * \retval FALSE   Startup failed; retain the owner through TryClose.
 */
bool_t MediaServer_Initialize(MediaServer_Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Queries whether the initialized pool accepts listener starts.
 *
 * \par Requirements:
 *  - 001 Final shutdown shall exclude every concurrent start caller.
 *  - 002 Readiness shall not imply that a listener slot is currently free.
 *
 * \param[in] workspace   Initialized retained lifetime
 *
 * \retval TRUE    The admission worker remains available.
 * \retval FALSE   Startup failed or shutdown/failure prevents new work.
 */
bool_t MediaServer_IsReady(const MediaServer_Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Gets retained resource/native failure evidence without native calls.
 *
 * \par Requirements:
 *  - 001 Owner, memory, admission and pool errors shall be queried in priority order.
 *  - 002 Failure evidence shall remain available after final closure.
 *
 * \param[in] workspace   Initialized owner, including after closure
 *
 * \retval 0         No resource failure is recorded.
 * \retval nonzero   A retained backend error code.
 */
uint32_t MediaServer_GetError(const MediaServer_Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Gets the media type associated with a terminated UTF-16 file extension.
 *
 * \par Requirements:
 *  - 001 Recognized extensions shall match without case sensitivity.
 *  - 002 NULL and unrecognized extensions shall select application/octet-stream.
 *  - 003 The returned terminated string shall remain immutable for program lifetime.
 *
 * \param[in] pExtension   Optional terminated extension, including its leading period
 *
 * \retval type   Borrowed terminated media type.
 */
const char* MediaServer_GetContentTypeForExtension(const uint16_t* pExtension);

/*************************************************************************************************/
/**
 * \brief Gets the final native port of a retained listener.
 *
 * \par Requirements:
 *  - 001 Required listener ownership shall remain valid until the caller successfully stops it.
 *
 * \param[in] pServer   Required published listener
 *
 * \retval port   The bound TCP port.
 */
uint16_t MediaServer_GetPort(const MediaServer* pServer);

/*************************************************************************************************/
/**
 * \brief Derives the first satisfiable member after validating a complete byte-range set.
 *
 * \par Requirements:
 *  - 001 Required result storage shall assert; results shall remain unchanged unless a satisfiable range is selected.
 *  - 002 Input shall terminate within 8192 bytes; empty files, malformed syntax and more than 32 members shall ignore Range.
 *
 * \param[in] pValue   Optional terminated Range field value
 * \param[in] size   Representation size
 * \param[out] pFirst   Required first-byte result
 * \param[out] pLast   Required inclusive final-byte result
 *
 * \retval 1   A satisfiable interval was published.
 * \retval 0   Ignore the Range field.
 * \retval -1   Valid syntax has no satisfiable member.
 */
int MediaServer_ParseRange(const char* pValue, uint64_t size, uint64_t* pFirst, uint64_t* pLast);

/*************************************************************************************************/
/**
 * \brief Publishes a fixed file representation through the startup pool.
 *
 * \par Requirements:
 *  - 001 Required initialized workspace and content-type storage shall assert; nonempty error capacity requires output storage.
 *  - 002 Paths shall be terminated UTF-16 within MAX_PATH and content types within 64 bytes; invalid input shall fail before publication.
 *  - 003 Concurrent starts may reserve different fixed slots; each returned listener shall have one serialized owner.
 *  - 004 Failed starts shall return no listener and preserve native cleanup inside the workspace.
 *  - 005 No application storage allocation or worker creation shall occur; runtime native socket/file operations remain open goal defects.
 *  - 006 Listener capacity exhaustion shall fail explicitly; occupied client workers shall retain new connections in the native backlog.
 *
 * \param[in,out] workspace   Ready startup pool, retained through all listener stops
 * \param[in] pFile   Optional terminated native-order UTF-16 media path
 * \param[in] pBindAddress   Terminated IPv4 address; NULL explicitly selects all interfaces
 * \param[in] port   Requested port; zero permits native selection
 * \param[in] pContentType   Terminated media type
 * \param[out] pError   Diagnostic storage when errorCapacity is nonzero
 * \param[in] errorCapacity   Writable diagnostic bytes including termination
 *
 * \retval server   Retained active listener; caller must stop it.
 * \retval NULL   No representation was published.
 */
MediaServer* MediaServer_Start(MediaServer_Workspace_t* workspace,
                               const uint16_t*          pFile,
                               const char*              pBindAddress,
                               uint16_t                 port,
                               const char*              pContentType,
                               char*                    pError,
                               uint32_t                 errorCapacity);

/*************************************************************************************************/
/**
 * \brief Publishes a file and immutable borrowed WebVTT through one listener.
 *
 * \par Requirements:
 *  - 001 Required initialized workspace and content-type storage shall assert; nonempty error capacity requires output storage.
 *  - 002 Paths shall be terminated UTF-16 within MAX_PATH and content types within 64 bytes; invalid input shall fail before publication.
 *  - 003 Concurrent starts may reserve different fixed slots; each returned listener shall have one serialized owner.
 *  - 004 Failed starts shall return no listener and preserve native cleanup inside the workspace.
 *  - 005 No application storage allocation or worker creation shall occur; runtime native socket/file operations remain open goal defects.
 *  - 006 Listener capacity exhaustion shall fail explicitly; occupied client workers shall retain new connections in the native backlog.
 *  - 007 Nonempty WebVTT requires readable storage; its bytes shall remain alive and unchanged through successful TryStop.
 *  - 008 NULL pFile permits subtitle hosting for an independently hosted video.
 *
 * \param[in,out] workspace   Ready startup pool, retained through all listener stops
 * \param[in] pFile   Optional terminated native-order UTF-16 media path
 * \param[in] pBindAddress   Terminated IPv4 address; NULL explicitly selects all interfaces
 * \param[in] port   Requested port; zero permits native selection
 * \param[in] pContentType   Terminated media type
 * \param[in] pVtt   Immutable WebVTT borrow
 * \param[in] vttSize   Exact readable WebVTT bytes
 * \param[out] pError   Diagnostic storage when errorCapacity is nonzero
 * \param[in] errorCapacity   Writable diagnostic bytes including termination
 *
 * \retval server   Retained listener and subtitle borrow.
 * \retval NULL   No borrow remains with a serving worker.
 */
MediaServer* MediaServer_StartWithSubtitles(MediaServer_Workspace_t* workspace,
                                            const uint16_t*          pFile,
                                            const char*              pBindAddress,
                                            uint16_t                 port,
                                            const char*              pContentType,
                                            const byte_t*              pVtt,
                                            uint64_t                 vttSize,
                                            char*                    pError,
                                            uint32_t                 errorCapacity);

/*************************************************************************************************/
/**
 * \brief Publishes a growing file whose live end is observed during transfer.
 *
 * \par Requirements:
 *  - 001 Required initialized workspace and content-type storage shall assert; nonempty error capacity requires output storage.
 *  - 002 Paths shall be terminated UTF-16 within MAX_PATH and content types within 64 bytes; invalid input shall fail before publication.
 *  - 003 Concurrent starts may reserve different fixed slots; each returned listener shall have one serialized owner.
 *  - 004 Failed starts shall return no listener and preserve native cleanup inside the workspace.
 *  - 005 No application storage allocation or worker creation shall occur; runtime native socket/file operations remain open goal defects.
 *  - 006 Listener capacity exhaustion shall fail explicitly; occupied client workers shall retain new connections in the native backlog.
 *  - 007 The producer shall mark completion when no more bytes will be appended.
 *  - 008 Growing transfer waits and native I/O remain incomplete realtime behavior.
 *
 * \param[in,out] workspace   Ready startup pool, retained through all listener stops
 * \param[in] pFile   Optional terminated native-order UTF-16 media path
 * \param[in] pBindAddress   Terminated IPv4 address; NULL explicitly selects all interfaces
 * \param[in] port   Requested port; zero permits native selection
 * \param[in] pContentType   Terminated media type
 * \param[out] pError   Diagnostic storage when errorCapacity is nonzero
 * \param[in] errorCapacity   Writable diagnostic bytes including termination
 *
 * \retval server   Retained growing listener.
 * \retval NULL   No representation was published.
 */
MediaServer* MediaServer_StartGrowing(MediaServer_Workspace_t* workspace,
                                      const uint16_t*          pFile,
                                      const char*              pBindAddress,
                                      uint16_t                 port,
                                      const char*              pContentType,
                                      char*                    pError,
                                      uint32_t                 errorCapacity);

/*************************************************************************************************/
/**
 * \brief Publishes producer completion to readers of a growing representation.
 *
 * \par Details:
 *  Growing readers can stop at the final available bytes.
 *
 * \par Requirements:
 *  - 001 The caller shall retain listener ownership; NULL and fixed representations shall be harmless.
 *
 * \param[in,out] pServer   Optional retained listener
 *
 * \retval None No value is returned.
 */
void MediaServer_MarkComplete(MediaServer* pServer);

/*************************************************************************************************/
/**
 * \brief Requests shutdown and polls retained listener and client ownership.
 *
 * \par Requirements:
 *  - 001 The required owner address shall assert when absent; a NULL owned value shall succeed.
 *  - 002 The caller shall serialize owner API access and retain Winsock, the owner and served files until TRUE.
 *  - 003 Pending admission/client cleanup shall preserve the owner; normal polling shall perform no native cleanup; failed admission may trigger exclusive recovery cleanup.
 *  - 004 Admission shall close the listener without concurrent accept and retain failed native releases.
 *  - 005 Admission shall acquire worker completion and release every client handle before publishing STOPPED.
 *  - 006 FALSE shall preserve the owner value; TRUE shall clear it and remain idempotent.
 *  - 007 Runtime native socket/file creation, blocking I/O and provider allocation remain separate defects.
 *
 * \param[in,out] owner   Retained server pointer, possibly already NULL
 *
 * \retval        TRUE    No callback can access server state or issue further client I/O.
 * \retval        FALSE   Work or native release remains pending; retry with the same owner.
 */
bool_t MediaServer_TryStop(MediaServer** owner);

/*************************************************************************************************/
/**
 * \brief Closes startup workers and storage after all public listener owners retire.
 *
 * \par Requirements:
 *  - 001 The caller shall end all concurrent starts and successfully stop all returned listeners first.
 *  - 002 First closure shall revoke readiness; admission shall retain cleanup for abandoned failed starts.
 *  - 003 Client worker closure shall precede storage release; failed native releases remain retryable.
 *  - 004 The owner and Winsock shall remain live through TRUE; repeated successful closure shall be valid.
 *
 * \param[in,out] workspace   Initialized owner, including after failed startup
 *
 * \retval TRUE    No native resource or callback borrow remains.
 * \retval FALSE   Cleanup is pending; retain owner and dependencies.
 */
bool_t MediaServer_TryClose(MediaServer_Workspace_t* workspace);

#endif /* CAST_MEDIA_SERVER_H */
