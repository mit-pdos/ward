
.globl __x86_indirect_thunk_r11
__x86_indirect_thunk_r11:
	callq 2f
1:	pause
	lfence
	jmp 1b
.align 16
2:	mov    %r11,(%rsp)
	retq
