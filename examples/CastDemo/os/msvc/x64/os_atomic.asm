;=================================================================================================
; 1. References
;===============================================================================================

; Intel SDM Volume 3, memory ordering: naturally aligned ordinary accesses and
; locked read-modify-write operations in normally cacheable process memory.
; https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf
; Calls are external MSVC x64 compiler boundaries; all routines are leaf functions.
; Load acquires, Store releases, and locked operations acquire and release.
; These interfaces do not promise sequential consistency across separate objects.

;=================================================================================================
; 2. INCLUDE FILES
;===============================================================================================

;=================================================================================================
; 3. DECLARATIONS
;
; 3.1 Macros
;===============================================================================================

OPTION CASEMAP:NONE

;=================================================================================================
; 3.2 Types
;===============================================================================================

;=================================================================================================
; 3.3 External global variables
;===============================================================================================

;=================================================================================================
; 3.4 Static global variables
;===============================================================================================

;=================================================================================================
; 3.5 Static function prototypes
;===============================================================================================

PUBLIC OS_Atomic_Load32Core
PUBLIC OS_Atomic_Load64Core
PUBLIC OS_Atomic_Store32Core
PUBLIC OS_Atomic_Store64Core
PUBLIC OS_Atomic_Exchange32Core
PUBLIC OS_Atomic_Exchange64Core
PUBLIC OS_Atomic_CompareExchange32Core
PUBLIC OS_Atomic_CompareExchange64Core
PUBLIC OS_Atomic_FetchAdd32Core
PUBLIC OS_Atomic_FetchAdd64Core

.code

;=================================================================================================
; 4. PRIVATE FUNCTIONS
;===============================================================================================

;=================================================================================================
; 5. PUBLIC FUNCTIONS
;===============================================================================================

;*************************************************************************************************
; Details: One naturally aligned load acquires on x64; no locked read or retry is needed.
OS_Atomic_Load32Core PROC
    mov     eax, DWORD PTR [rcx]
    ret
OS_Atomic_Load32Core ENDP

;*************************************************************************************************
; Details: One naturally aligned load acquires on x64; no locked read or retry is needed.
OS_Atomic_Load64Core PROC
    mov     rax, QWORD PTR [rcx]
    ret
OS_Atomic_Load64Core ENDP

;*************************************************************************************************
; Details: One naturally aligned store releases preceding ordinary stores on x64.
OS_Atomic_Store32Core PROC
    mov     DWORD PTR [rcx], edx
    ret
OS_Atomic_Store32Core ENDP

;*************************************************************************************************
; Details: One naturally aligned store releases preceding ordinary stores on x64.
OS_Atomic_Store64Core PROC
    mov     QWORD PTR [rcx], rdx
    ret
OS_Atomic_Store64Core ENDP

;*************************************************************************************************
; Details: XCHG with memory is implicitly locked; return the replaced word.
OS_Atomic_Exchange32Core PROC
    mov     eax, edx
    xchg    DWORD PTR [rcx], eax
    ret
OS_Atomic_Exchange32Core ENDP

;*************************************************************************************************
; Details: XCHG with memory is implicitly locked; return the replaced word.
OS_Atomic_Exchange64Core PROC
    mov     rax, rdx
    xchg    QWORD PTR [rcx], rax
    ret
OS_Atomic_Exchange64Core ENDP

;*************************************************************************************************
; Details: One locked strong comparison returns the prior word, including on mismatch.
OS_Atomic_CompareExchange32Core PROC
    mov     eax, edx
    lock cmpxchg DWORD PTR [rcx], r8d
    ret
OS_Atomic_CompareExchange32Core ENDP

;*************************************************************************************************
; Details: One locked strong comparison returns the prior word, including on mismatch.
OS_Atomic_CompareExchange64Core PROC
    mov     rax, rdx
    lock cmpxchg QWORD PTR [rcx], r8
    ret
OS_Atomic_CompareExchange64Core ENDP

;*************************************************************************************************
; Details: One locked unsigned addition returns the prior word; overflow wraps modulo width.
OS_Atomic_FetchAdd32Core PROC
    mov     eax, edx
    lock xadd DWORD PTR [rcx], eax
    ret
OS_Atomic_FetchAdd32Core ENDP

;*************************************************************************************************
; Details: One locked unsigned addition returns the prior word; overflow wraps modulo width.
OS_Atomic_FetchAdd64Core PROC
    mov     rax, rdx
    lock xadd QWORD PTR [rcx], rax
    ret
OS_Atomic_FetchAdd64Core ENDP

END
