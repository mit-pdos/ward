#include <string.h>
#include "types.h"
#include "kernel.hh"

void* qtext;

// extern u64 __x86_indirect_thunk_rax;
// extern u64 __x86_indirect_thunk_rcx;
// extern u64 __x86_indirect_thunk_rdx;
// extern u64 __x86_indirect_thunk_rbx;
// extern u64 __x86_indirect_thunk_rsi;
// extern u64 __x86_indirect_thunk_r8;
// extern u64 __x86_indirect_thunk_r9;
// extern u64 __x86_indirect_thunk_r10;
// extern u64 __x86_indirect_thunk_r12;
// extern u64 __x86_indirect_thunk_r13;
// extern u64 __x86_indirect_thunk_r14;
// extern u64 __x86_indirect_thunk_r15;

// void* retpoline_thunks[] = {
//     (void*)&__x86_indirect_thunk_rax,
//     (void*)&__x86_indirect_thunk_rcx,
//     (void*)&__x86_indirect_thunk_rdx,
//     (void*)&__x86_indirect_thunk_rbx,
//     (void*)&__x86_indirect_thunk_rsi,
//     (void*)&__x86_indirect_thunk_r8,
//     (void*)&__x86_indirect_thunk_r9,
//     (void*)&__x86_indirect_thunk_r10,
//     (void*)&__x86_indirect_thunk_r12,
//     (void*)&__x86_indirect_thunk_r13,
// };

// void* text_to_qtext(void* addr) {
//   return (char*)addr - KCODE + (u64)qtext;
// }

// void replace_qtext_u16(void* target, u16 value) {
//     *(u16*)text_to_qtext(target) = value;
// }
// void replace_qtext_u32(void* target, u32 value) {
//     *(u32*)text_to_qtext(target) = value;
// }

// void remove_retpolines()
// {
//     replace_qtext_u16(&__x86_indirect_thunk_rax, 0xE0FF);
//     replace_qtext_u16(&__x86_indirect_thunk_rcx, 0xE1FF);
//     replace_qtext_u16(&__x86_indirect_thunk_rdx, 0xE2FF);
//     replace_qtext_u16(&__x86_indirect_thunk_rbx, 0xE3FF);
//     replace_qtext_u16(&__x86_indirect_thunk_rsi, 0xE6FF);
//     replace_qtext_u32(&__x86_indirect_thunk_r8, 0xE0FF41);
//     replace_qtext_u32(&__x86_indirect_thunk_r9, 0xE1FF41);
//     replace_qtext_u32(&__x86_indirect_thunk_r10, 0xE2FF41);
//     replace_qtext_u32(&__x86_indirect_thunk_r12, 0xE4FF41);
//     replace_qtext_u32(&__x86_indirect_thunk_r13, 0xE5FF41);
// }

void inithotpatch()
{
    qtext = kalloc("qtext", 0x200000);
    memset(qtext, 0, 0x100000);
    memmove(qtext + 0x100000, (void*)KCODE + 0x100000, 0x100000);
    // remove_retpolines();
}
