;=================================================================================================
; 1. References
;=================================================================================================
; Microsoft x64 prolog and epilog: compiler stack-probe calling convention.
; https://learn.microsoft.com/en-us/cpp/build/prolog-and-epilog
; RAX is the pending frame extent. Only R10, R11 and condition codes may change.
; The caller adjusts RSP after this leaf returns. Windows x64 uses 4096-byte pages.

;=================================================================================================
; 2. INCLUDE FILES
;=================================================================================================

;=================================================================================================
; 3. DECLARATIONS
;
; 3.1 Macros
;=================================================================================================

CRT_STACK_PAGE_BYTES   EQU 4096
CRT_STACK_RETURN_BYTES EQU 8

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
; CRT_Stack_ProbeCore:
;   RAX pending frame size; RSP points at the helper return address.
;   Every page in the pending stack extent is probed in descending order.
;   Zero extent performs no memory access. RAX, RSP, argument registers,
;   nonvolatile registers, SIMD registers and the direction flag are preserved.
;   This is a compiler backend ABI, not a callable C function signature.

PUBLIC CRT_Stack_ProbeCore

.code

;=================================================================================================
; 4. PRIVATE FUNCTIONS
;=================================================================================================

;=================================================================================================
; 5. PUBLIC FUNCTIONS
;=================================================================================================

;*************************************************************************************************
; Details: Each full-page step is loaded before proceeding to the next page.
; The final partial step reaches the exact pending stack address. Guard faults
; retain the caller's original stack and return address for normal unwinding.
CRT_Stack_ProbeCore PROC
    lea     r10, [rsp + CRT_STACK_RETURN_BYTES]
    mov     r11, rax
probe_pages:
    cmp     r11, CRT_STACK_PAGE_BYTES
    jb      probe_tail
    sub     r10, CRT_STACK_PAGE_BYTES
    test    BYTE PTR [r10], 0
    sub     r11, CRT_STACK_PAGE_BYTES
    jmp     probe_pages
probe_tail:
    test    r11, r11
    jz      probe_done
    sub     r10, r11
    test    BYTE PTR [r10], 0
probe_done:
    ret
CRT_Stack_ProbeCore ENDP

END
