/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Native filesystem encoding is UTF-16, selected for this backend independently
 * of application/UI character selection. Win32 names and text remain generic.
 * https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-gettempfilenamew
 * https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h> // IWYU pragma: keep
#include <minwindef.h>
#include <winnt.h>
#include <fileapi.h>
#include <errhandlingapi.h>
#include <winerror.h>
#include <stdint.h>
#include "os/os_temporaryfile.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "crt/crt_memory.h"
#include "utl/utl_unicode.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEMPORARYFILE_PREFIX          (TEXT("cvk"))
#define TEMPORARYFILE_SUFFIX_CAPACITY (14) ///< GetTempFileName's documented suffix allowance.

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

/*************************************************************************************************/
/**
 * \brief Retains the first native failure, including an absent native error code.
 *
 * \par Details:
 *  First failure evidence is retained.
 *
 * \par Requirements:
 *  - 001 The file owner shall already be initialized and exclusively held.
 *  - 002 A failure with a zero error code shall retain ERROR_GEN_FAILURE.
 *  - 003 Earlier failure evidence shall remain unchanged.
 *
 * \param[in,out] file   Initialized file owner
 * \param[in]     code   Native error captured immediately after failure
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void RecordError(OS_TemporaryFile_t* file, uint32_t code);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A successful error value cannot represent an unsuccessful native operation.
 */
static FORCE_INLINE void RecordError(OS_TemporaryFile_t* file, uint32_t code)
{
    uint32_t error;

    error = code;
    if (ERROR_SUCCESS == error)
    {
        error = ERROR_GEN_FAILURE;
    }
    if (ERROR_SUCCESS == file->error)
    {
        file->error = error;
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The zero-unique native operation creates the filesystem entry and closes its
 *  creation handle. A successful path stays owned even if later consumers fail.
 */
extern FORCE_INLINE bool_t OS_TemporaryFile_Initialize(OS_TemporaryFile_t* file)
{
    DWORD    length;
    TCHAR    directory[OS_TEMPORARYFILE_PATH_CAPACITY];
    bool_t   has_directory;
    bool_t   fits_directory;
    UINT     unique;
    uint32_t error;

    LOGASSERT(NULL != file);
    LOGASSERT(sizeof(TCHAR) == sizeof(uint16_t));
    LOGASSERT(OS_TEMPORARYFILE_PATH_CAPACITY == MAX_PATH);
    CRT_Memory_Set(file->path, 0, sizeof(file->path));
    file->ready    = FALSE;
    file->owned    = FALSE;
    file->error    = 0;
    length         = GetTempPath(OS_TEMPORARYFILE_PATH_CAPACITY, directory);
    has_directory  = (0 < length);
    fits_directory = (length <= (OS_TEMPORARYFILE_PATH_CAPACITY - TEMPORARYFILE_SUFFIX_CAPACITY));
    if (has_directory && fits_directory)
    {
        unique = GetTempFileName(directory, TEMPORARYFILE_PREFIX, 0, (LPTSTR)file->path);
        if (0 < unique)
        {
            file->owned = TRUE;
            file->ready = TRUE;
        }
        else
        {
            error = GetLastError();
            RecordError(file, error);
        }
    }
    else if (!has_directory)
    {
        error = GetLastError();
        RecordError(file, error);
    }
    else
    {
        RecordError(file, ERROR_BUFFER_OVERFLOW);
    }
    return file->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Readiness is owner-serialized and changes only at initialization or shutdown.
 */
extern FORCE_INLINE bool_t OS_TemporaryFile_IsReady(const OS_TemporaryFile_t* file)
{
    LOGASSERT(NULL != file);
    return file->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t OS_TemporaryFile_GetError(const OS_TemporaryFile_t* file)
{
    LOGASSERT(NULL != file);
    return file->error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An unavailable owner exposes no new path borrow.
 */
extern FORCE_INLINE const uint16_t* OS_TemporaryFile_GetPath(const OS_TemporaryFile_t* file)
{
    const uint16_t* path;

    LOGASSERT(NULL != file);
    path = NULL;
    if (file->ready)
    {
        path = file->path;
    }
    return path;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Readiness is revoked before native removal. Failure retains both the path
 *  identity and first error; successful removal clears the identity once.
 */
extern FORCE_INLINE bool_t OS_TemporaryFile_TryClose(OS_TemporaryFile_t* file)
{
    bool_t   removed;
    uint32_t error;

    LOGASSERT(NULL != file);
    file->ready = FALSE;
    removed     = !file->owned;
    if (!removed)
    {
        removed = DeleteFile((LPCTSTR)file->path);
        if (!removed)
        {
            error   = GetLastError();
            removed = (ERROR_FILE_NOT_FOUND == error) || (ERROR_PATH_NOT_FOUND == error);
            if (!removed)
            {
                RecordError(file, error);
            }
        }
        if (removed)
        {
            file->owned   = FALSE;
            file->path[0] = UTL_UNICODE_TERMINATOR;
        }
    }
    return removed;
}
