/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Win32-only protected-page fixture for the hand-written CRT backend. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_process.h"
#include <windows.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_LENGTH      (257)
#define TEST_PAGE_COUNT  (6)
#define TEST_OUTPUT_PAGE (4)
#define TEST_PLACEMENTS  (4)
#define TEST_INPUT_END   (1)
#define TEST_OUTPUT_END  (2)

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
 *  Allocate and protect fixture pages once during test initialization. Every tested
 *  extent abuts inaccessible storage so a vector overread or overwrite fails immediately.
 *  All subsequent memory operations reuse the initialized fixture.
 */
uint32_t OS_Application_Main(void)
{
    SYSTEM_INFO info;
    uint32_t    page_size;
    byte_t*     pages;
    BOOL        protected_page;
    DWORD       old_protection;
    int32_t     comparison;
    uint32_t    length;
    uint32_t    placement;
    byte_t*     input;
    byte_t*     output;
    uint32_t    index;
    BOOL        released;

    OS_Process_SetInteractive(FALSE);
    GetSystemInfo(&info);
    page_size = info.dwPageSize;
    LOGASSERT(TEST_LENGTH < page_size);
    LOGASSERT(page_size <= (UINT32_MAX / TEST_PAGE_COUNT));
    pages = VirtualAlloc(NULL, page_size * TEST_PAGE_COUNT, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    LOGASSERT(NULL != pages);
    protected_page = VirtualProtect(&pages[page_size], page_size, PAGE_READWRITE, &old_protection);
    LOGASSERT(protected_page);
    protected_page = VirtualProtect(&pages[page_size * TEST_OUTPUT_PAGE], page_size, PAGE_READWRITE, &old_protection);
    LOGASSERT(protected_page);

    CRT_Memory_Copy(pages, &pages[page_size * TEST_OUTPUT_PAGE], 0);
    CRT_Memory_Move(pages, &pages[page_size * TEST_OUTPUT_PAGE], 0);
    CRT_Memory_Set(pages, 0, 0);
    comparison = CRT_Memory_Compare(pages, &pages[page_size * TEST_OUTPUT_PAGE], 0);
    LOGASSERT(0 == comparison);

    for (length = 1; length <= TEST_LENGTH; ++length)
    {
        for (placement = 0; placement < TEST_PLACEMENTS; ++placement)
        {
            input  = &pages[page_size];
            output = &pages[page_size * TEST_OUTPUT_PAGE];
            if (placement & TEST_INPUT_END)
            {
                input += page_size - length;
            }
            if (placement & TEST_OUTPUT_END)
            {
                output += page_size - length;
            }
            for (index = 0; index < length; ++index)
            {
                input[index] = (byte_t)((index * 71) & UINT8_MAX);
            }
            CRT_Memory_Copy(output, input, length);
            comparison = CRT_Memory_Compare(output, input, length);
            LOGASSERT(0 == comparison);
            CRT_Memory_Set(output, UINT8_MAX, length);
            for (index = 0; index < length; ++index)
            {
                LOGASSERT(UINT8_MAX == output[index]);
            }
            CRT_Memory_Move(output, input, length);
            comparison = CRT_Memory_Compare(output, input, length);
            LOGASSERT(0 == comparison);
            if (1 < length)
            {
                CRT_Memory_Move(output + 1, output, length - 1);
                for (index = 1; index < length; ++index)
                {
                    LOGASSERT((byte_t)(((index - 1) * 71) & UINT8_MAX) == output[index]);
                }
                CRT_Memory_Move(output, output + 1, length - 1);
                for (index = 0; index < (length - 1); ++index)
                {
                    LOGASSERT((byte_t)((index * 71) & UINT8_MAX) == output[index]);
                }
            }
        }
    }
    released = VirtualFree(pages, 0, MEM_RELEASE);
    LOGASSERT(released);

    return 0;
}
