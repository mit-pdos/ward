
#define _OPTIONAL(option, value, opcode) \
    .section .rodata; \
91:  .asciz option; \
92:  .asciz value; \
    .previous; \
    .section .hotpatch, "a"; \
    .quad opcode, 91b, 92b, 93f; \
    .previous; \
    93:

#define OPTIONAL(option, value) \
    _OPTIONAL(option, value, 0x3)
#define OPTIONAL_KTEXT(option, value) \
    _OPTIONAL(option, value, 0x1)
#define OPTIONAL_QTEXT(option, value) \
    _OPTIONAL(option, value, 0x2)

#define OPTIONAL_OR_NOPS(option) \
    .section .hotpatch, "a"; \
    .quad 0x4, 0, 91f, 0; \
    .previous; \
    91:

#define OPTIONAL_OR_CALL(option, alternative) \
    .section .hotpatch, "a"; \
    .quad 0x5, alternative, 91f, 0; \
    .previous; \
    91:

#define OPTIONAL_OR_INSTR(option, instr) \
    .section .hotpatch, "a";             \
    .quad 0x6, 94f, 91f, 95f-94f;        \
    .previous;                           \
    .section .rodata;                    \
94: instr;                               \
95:                                      \
    .previous;                           \
91:

/*
 * Taken from Linux: Fill the CPU return stack buffer.
 *
 * Each entry in the RSB, if used for a speculative 'ret', contains an
 * infinite 'pause; lfence; jmp' loop to capture speculative execution.
 *
 * This is required in various cases for retpoline and IBRS-based
 * mitigations for the Spectre variant 2 vulnerability. Sometimes to
 * eliminate potentially bogus entries from the RSB, and sometimes
 * purely to ensure that it doesn't get empty, which on some CPUs would
 * allow predictions from other (unwanted!) sources to be used.
 *
 * We define a CPP macro such that it can be used from both .S files and
 * inline assembly. It's possible to do a .macro and then include that
 * from C via asm(".include <asm/nospec-branch.h>") but let's not go there.
 */

#define RSB_CLEAR_LOOPS        32    /* To forcibly overwrite all entries */
#define RSB_FILL_LOOPS        16    /* To avoid underflow */

/*
 * Google experimented with loop-unrolling and this turned out to be
 * the optimal version — two calls, each with their own speculation
 * trap should their return address end up getting used, in a loop.
 */
#define FILL_RETURN_BUFFER               \
    movq $(RSB_CLEAR_LOOPS/2), %rbx;     \
771:                                     \
    call    772f;                        \
773:    /* speculation trap */           \
    pause;                               \
    lfence;                              \
    jmp    773b;                         \
772:                                     \
    call    774f;                        \
775:    /* speculation trap */           \
    pause;                               \
    lfence;                              \
    jmp    775b;                         \
774:                                     \
    dec    %rbx;                         \
    jnz    771b;                         \
    addq    $(RSB_CLEAR_LOOPS * 8), %rsp;

