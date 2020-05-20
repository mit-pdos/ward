#include "asmdefines.h"
#include "memlayout.h"

/*
 * Fill the CPU return stack buffer.
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
#define __FILL_RETURN_BUFFER(scratch)    \
    movq $(RSB_CLEAR_LOOPS/2), scratch;  \
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
    dec    scratch;                      \
    jnz    771b;                         \
    addq    $(RSB_CLEAR_LOOPS * 8), %rsp;


# Context switch
#
#   void swtch(struct context **old, struct context *new);
#
# Save current register context in old
# and then load register context from new.
# %rdi holds old, %rsi holds new.
.globl swtch
swtch:
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15

        movq %rsp, (%rdi)
        movq %rsi, %rsp

        __FILL_RETURN_BUFFER(%rbx)

        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        ret

.globl swtch_and_barrier
swtch_and_barrier:
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15

        movq %rsp, (%rdi)
        movq %cr3, %rbx
        bts $(63), %rbx
        andq $(~0x1001), %rbx
        andq %gs:GS_CR3_MASK, %rbx
        movq %rbx, %cr3
        movq %rsi, %rsp

        __FILL_RETURN_BUFFER(%rbx)

        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        ret

.globl threadstub
threadstub:
        movq %r12, %rdi
        movq %r13, %rsi
        jmp threadhelper

// Switch to executing on the current proc's kstack.
//
// Preconditions:
//      1. Kernel GS in use
.globl switch_to_kstack
switch_to_kstack:
        cmpb $0, (secrets_mapped)
        jne 1f

        // Install kernel page tables
        movq %cr3, %rax
        bts $(63), %rax
        andq $(~0x1001), %rax
        andq %gs:GS_CR3_MASK, %rax
        movq %rax, %cr3

        // Load pointers to the top of the kstack and qstack.
        movq %gs:GS_PROC, %rax
        movq PROC_KSTACK(%rax), %rdi
        movq PROC_QSTACK(%rax), %rsi
        addq $(KSTACKSIZE), %rdi
        addq $(KSTACKSIZE), %rsi

        // Compute how much has been pushed onto the stack.
        movq %rdi, %rdx
        subq %rsp, %rdx

        // Jump to return if we are on the double-fault stack or NMI stack.
        cmpq $(KSTACKSIZE), %rdx
        ja 1f

        // Point both pointers to current position on the stack.
        subq %rdx, %rdi
        subq %rdx, %rsi

        call memcpy  // memcpy(dest=rdi, src=rsi, size=rdx);

1:      ret

