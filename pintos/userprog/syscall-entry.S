#include "threads/loader.h"

.text
.globl syscall_entry
.type syscall_entry, @function
syscall_entry:
	movq %rbx, temp1(%rip)
	movq %r12, temp2(%rip)     /* @lock callee가 저장해야 하는 레지스터. */
	movq %rsp, %rbx            /* @lock 사용자 영역 rsp를 저장한다. */
	movabs $tss, %r12
	movq (%r12), %r12
	movq 4(%r12), %rsp         /* @lock tss에서 ring0 rsp를 읽는다. */
	/* @lock 이제 커널 스택 안에 있다. */
	push $(SEL_UDSEG)      /* @lock if->ss. */
	push %rbx              /* @lock if->rsp. */
	push %r11              /* @lock if->eflags. */
	push $(SEL_UCSEG)      /* @lock if->cs. */
	push %rcx              /* @lock if->rip. */
	subq $16, %rsp         /* @lock error_code와 vec_no를 건너뛴다. */
	push $(SEL_UDSEG)      /* @lock if->ds. */
	push $(SEL_UDSEG)      /* @lock if->es. */
	push %rax
	movq temp1(%rip), %rbx
	push %rbx
	pushq $0
	push %rdx
	push %rbp
	push %rdi
	push %rsi
	push %r8
	push %r9
	push %r10
	pushq $0 /* @lock r11을 건너뛴다. */
	movq temp2(%rip), %r12
	push %r12
	push %r13
	push %r14
	push %r15
	movq %rsp, %rdi

check_intr:
	btsq $9, %r11          /* @lock 인터럽트를 복구할지 확인한다. */
	jnb no_sti
	sti                    /* @lock 인터럽트를 복구한다. */
no_sti:
	movabs $syscall_handler, %r12
	call *%r12
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp
	popq %rcx              /* @lock if->rip. */
	addq $8, %rsp
	popq %r11              /* @lock if->eflags. */
	popq %rsp              /* @lock if->rsp. */
	sysretq

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0
