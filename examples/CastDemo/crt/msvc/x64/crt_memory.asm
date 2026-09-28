;=================================================================================================
; 1. References
;=================================================================================================
; Microsoft x64 calling convention: RCX, RDX, R8 hold the first three arguments.
; https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention
; These leaf implementations use only volatile registers, preserve RSP and DF,
; and perform no calls. SSE2 is part of the x64 target contract.
; No access crosses a supplied byte extent, including vector and scalar tails.

;=================================================================================================
; 2. INCLUDE FILES
;=================================================================================================

;=================================================================================================
; 3. DECLARATIONS
;
; 3.1 Macros
;=================================================================================================

CRT_VECTOR_BYTES EQU 16
CRT_BLOCK_BYTES  EQU 64
CRT_BYTE_SPREAD  EQU 0101010101010101h

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
; CRT_Memory_CompareCore / memcmp:
;   RCX and RDX readable extents, R8 byte count; EAX first unsigned-byte difference.
; CRT_Memory_CopyCore / memcpy:
;   RCX output, RDX disjoint input, R8 byte count; RAX original output.
; CRT_Memory_MoveCore / memmove:
;   RCX output, RDX input, R8 byte count; overlap allowed; RAX original output.
; CRT_Memory_SetCore / memset:
;   RCX output, low DL repeated byte, R8 byte count; RAX original output.
; All counts have the x64 ABI's 64-bit extent. Core names are internal interfaces;
; the public CRT_Memory_* C functions assert the required pointer contracts.

PUBLIC CRT_Memory_CompareCore
PUBLIC CRT_Memory_CopyCore
PUBLIC CRT_Memory_MoveCore
PUBLIC CRT_Memory_SetCore

.code

;=================================================================================================
; 4. PRIVATE FUNCTIONS
;=================================================================================================

;=================================================================================================
; 5. PUBLIC FUNCTIONS
;=================================================================================================

;*************************************************************************************************
; Details: Skip equal qwords, then find the first differing unsigned byte.
; The byte path also handles the last zero to seven bytes without overreading.
CRT_Memory_CompareCore PROC
    xor     eax, eax
compare_qwords:
    cmp     r8, 8
    jb      compare_bytes
    mov     r9, QWORD PTR [rcx]
    cmp     r9, QWORD PTR [rdx]
    jne     compare_bytes
    add     rcx, 8
    add     rdx, 8
    sub     r8, 8
    jmp     compare_qwords
compare_bytes:
    test    r8, r8
    jz      compare_done
    movzx   eax, BYTE PTR [rcx]
    movzx   r9d, BYTE PTR [rdx]
    sub     eax, r9d
    jne     compare_done
    inc     rcx
    inc     rdx
    dec     r8
    jmp     compare_bytes
compare_done:
    ret
CRT_Memory_CompareCore ENDP

;*************************************************************************************************
; Details: Copy complete 64-byte blocks, then exact 16/8/4/2/1-byte tails.
; Each block is fully loaded before its stores, also supporting forward overlap
; when reached from CRT_Memory_MoveCore. Nonvolatile registers and the direction flag stay intact.
CRT_Memory_CopyCore PROC
    mov     rax, rcx
copy_blocks:
    cmp     r8, CRT_BLOCK_BYTES
    jb      copy_vectors
    movdqu  xmm0, XMMWORD PTR [rdx]
    movdqu  xmm1, XMMWORD PTR [rdx + 16]
    movdqu  xmm2, XMMWORD PTR [rdx + 32]
    movdqu  xmm3, XMMWORD PTR [rdx + 48]
    movdqu  XMMWORD PTR [rcx], xmm0
    movdqu  XMMWORD PTR [rcx + 16], xmm1
    movdqu  XMMWORD PTR [rcx + 32], xmm2
    movdqu  XMMWORD PTR [rcx + 48], xmm3
    add     rcx, CRT_BLOCK_BYTES
    add     rdx, CRT_BLOCK_BYTES
    sub     r8, CRT_BLOCK_BYTES
    jmp     copy_blocks
copy_vectors:
    cmp     r8, CRT_VECTOR_BYTES
    jb      copy_qword
    movdqu  xmm0, XMMWORD PTR [rdx]
    movdqu  XMMWORD PTR [rcx], xmm0
    add     rcx, CRT_VECTOR_BYTES
    add     rdx, CRT_VECTOR_BYTES
    sub     r8, CRT_VECTOR_BYTES
    jmp     copy_vectors
copy_qword:
    test    r8b, 8
    jz      copy_dword
    mov     r9, QWORD PTR [rdx]
    mov     QWORD PTR [rcx], r9
    add     rcx, 8
    add     rdx, 8
copy_dword:
    test    r8b, 4
    jz      copy_word
    mov     r9d, DWORD PTR [rdx]
    mov     DWORD PTR [rcx], r9d
    add     rcx, 4
    add     rdx, 4
copy_word:
    test    r8b, 2
    jz      copy_byte
    mov     r9w, WORD PTR [rdx]
    mov     WORD PTR [rcx], r9w
    add     rcx, 2
    add     rdx, 2
copy_byte:
    test    r8b, 1
    jz      copy_done
    mov     r9b, BYTE PTR [rdx]
    mov     BYTE PTR [rcx], r9b
copy_done:
    ret
CRT_Memory_CopyCore ENDP

;*************************************************************************************************
; Details: Forward order is safe below the source and for disjoint extents.
; A higher overlapping destination is copied from the end in exact chunks.
; Direction selection uses subtraction so end-address addition cannot overflow.
CRT_Memory_MoveCore PROC
    mov     rax, rcx
    cmp     rcx, rdx
    je      move_done
    jb      CRT_Memory_CopyCore
    mov     r9, rcx
    sub     r9, rdx
    cmp     r9, r8
    jae     CRT_Memory_CopyCore
move_vectors:
    cmp     r8, CRT_VECTOR_BYTES
    jb      move_qword
    sub     r8, CRT_VECTOR_BYTES
    movdqu  xmm0, XMMWORD PTR [rdx + r8]
    movdqu  XMMWORD PTR [rcx + r8], xmm0
    jmp     move_vectors
move_qword:
    test    r8b, 8
    jz      move_dword
    sub     r8, 8
    mov     r9, QWORD PTR [rdx + r8]
    mov     QWORD PTR [rcx + r8], r9
move_dword:
    test    r8b, 4
    jz      move_word
    sub     r8, 4
    mov     r9d, DWORD PTR [rdx + r8]
    mov     DWORD PTR [rcx + r8], r9d
move_word:
    test    r8b, 2
    jz      move_byte
    sub     r8, 2
    mov     r9w, WORD PTR [rdx + r8]
    mov     WORD PTR [rcx + r8], r9w
move_byte:
    test    r8b, 1
    jz      move_done
    mov     r9b, BYTE PTR [rdx]
    mov     BYTE PTR [rcx], r9b
move_done:
    ret
CRT_Memory_MoveCore ENDP

;*************************************************************************************************
; Details: Spread the unsigned low byte through a qword and SSE2 vector, then
; write complete blocks and exact tails. No aligned access or global state is needed.
CRT_Memory_SetCore PROC
    mov     rax, rcx
    test    r8, r8
    jz      set_done
    movzx   edx, dl
    mov     r9, CRT_BYTE_SPREAD
    imul    rdx, r9
    movq    xmm0, rdx
    punpcklqdq xmm0, xmm0
set_blocks:
    cmp     r8, CRT_BLOCK_BYTES
    jb      set_vectors
    movdqu  XMMWORD PTR [rcx], xmm0
    movdqu  XMMWORD PTR [rcx + 16], xmm0
    movdqu  XMMWORD PTR [rcx + 32], xmm0
    movdqu  XMMWORD PTR [rcx + 48], xmm0
    add     rcx, CRT_BLOCK_BYTES
    sub     r8, CRT_BLOCK_BYTES
    jmp     set_blocks
set_vectors:
    cmp     r8, CRT_VECTOR_BYTES
    jb      set_qword
    movdqu  XMMWORD PTR [rcx], xmm0
    add     rcx, CRT_VECTOR_BYTES
    sub     r8, CRT_VECTOR_BYTES
    jmp     set_vectors
set_qword:
    test    r8b, 8
    jz      set_dword
    mov     QWORD PTR [rcx], rdx
    add     rcx, 8
set_dword:
    test    r8b, 4
    jz      set_word
    mov     DWORD PTR [rcx], edx
    add     rcx, 4
set_word:
    test    r8b, 2
    jz      set_byte
    mov     WORD PTR [rcx], dx
    add     rcx, 2
set_byte:
    test    r8b, 1
    jz      set_done
    mov     BYTE PTR [rcx], dl
set_done:
    ret
CRT_Memory_SetCore ENDP

END
