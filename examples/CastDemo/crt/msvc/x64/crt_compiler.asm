;=================================================================================================
; 1. References
;=================================================================================================
; Microsoft x64 compiler-helper ABI. These leaf adapters are linked only
; into executables that replace the system runtime. Core implementations have
; explicit CRT_ names and can be migrated independently of existing CRT callers.

;=================================================================================================
; 2. INCLUDE FILES
;=================================================================================================

;=================================================================================================
; 3. DECLARATIONS
;
; 3.1 Macros
;=================================================================================================

OPTION CASEMAP:NONE

;=================================================================================================
; 3.2 Types
;=================================================================================================

;=================================================================================================
; 3.3 External global variables
;=================================================================================================

;=================================================================================================
; 3.4 Static global variables
;=================================================================================================

;=================================================================================================
; 3.5 Static function prototypes
;=================================================================================================

EXTERN CRT_Memory_CompareCore:PROC
EXTERN CRT_Memory_CopyCore:PROC
EXTERN CRT_Memory_MoveCore:PROC
EXTERN CRT_Memory_SetCore:PROC
EXTERN CRT_Stack_ProbeCore:PROC
PUBLIC memcmp
PUBLIC memcpy
PUBLIC memmove
PUBLIC memset
PUBLIC __chkstk

.code

;=================================================================================================
; 4. PRIVATE FUNCTIONS
;=================================================================================================

;=================================================================================================
; 5. PUBLIC FUNCTIONS
;=================================================================================================

;*************************************************************************************************
; Details: Preserve all arguments; the core returns the signed first-byte difference.
memcmp PROC
    jmp CRT_Memory_CompareCore
memcmp ENDP

;*************************************************************************************************
; Details: Preserve all arguments; the core returns the original destination.
memcpy PROC
    jmp CRT_Memory_CopyCore
memcpy ENDP

;*************************************************************************************************
; Details: Preserve all arguments and the core's overlap-safe ordering.
memmove PROC
    jmp CRT_Memory_MoveCore
memmove ENDP

;*************************************************************************************************
; Details: The core reads only DL, implementing conversion of int to unsigned byte.
memset PROC
    jmp CRT_Memory_SetCore
memset ENDP

;*************************************************************************************************
; Details: Tail dispatch preserves RAX and the original compiler return address.
; The core probes the pending frame without adjusting the caller's stack.
__chkstk PROC
    jmp CRT_Stack_ProbeCore
__chkstk ENDP

END
