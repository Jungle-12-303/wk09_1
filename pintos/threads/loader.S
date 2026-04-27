





#include "threads/loader.h"


#define CR0_PE 0x00000001
#define CR0_EM 0x00000004
#define CR0_PG 0x80000000
#define CR0_WP 0x00010000



.globl start
start:

	.code16


	cli
	cld


	subw %ax, %ax
	movw %ax, %es
	movw %ax, %ds


	movw %ax, %ss
	movw $0x7c00, %sp




1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b


	movb $0xd1, %al
	outb %al, $0x64


1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b


	movb $0xdf, %al
	outb %al, $0x60



	mov $0xe820, %eax
	mov $(E820_MAP4), %edi
	xor %ebx, %ebx
	mov $0x534d4150, %edx
	mov $24, %ecx
	int $0x15
	cmp %eax, %edx
	test %ebx, %ebx
	je panic
	mov $24, %ebp


parse_e820:
	mov %ecx, -4(%edi)
	add $24, %edi
	mov $0xe820, %eax
	mov $24, %ecx
	int $0x15
	jc e820_parse_done
	add $24, %ebp
	test %ebx, %ebx
	jne parse_e820


e820_parse_done:
	mov %ecx, -4(%edi)
	movl $0x40, MULTIBOOT_FLAG
	movl %ebp, MULTIBOOT_MMAP_LEN
	movl $(E820_MAP), MULTIBOOT_MMAP_ADDR





	data32 lgdt gdtdesc


	movl %cr0, %eax
	orl $CR0_PE, %eax
	movl %eax, %cr0



	data32 ljmp $SEL_KCSEG, $protcseg


	.code32

protcseg:

	movw $SEL_KDSEG, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss



	movl $LOADER_PHYS_BASE + 0x30000, %esp



	movl $1, %ebx
	movl $LOADER_PHYS_BASE, %edi



	movw $0x3f6, %dx
	movb $0x02, %al
	outb %al, %dx


read_sector:


	movl $0x1f7, %edx
1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b


	movl $0x1f2, %edx
	movb $1, %al
	outb %al, %dx



	movl %ebx, %eax
	andl $0x0fffffff, %eax
	orl $0xe0000000, %eax



	movl $4, %ecx
1:	incw %dx
	outb %al, %dx
	shrl $8, %eax
	loop 1b


	incw %dx
	movb $0x20, %al
	outb %al, %dx


1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b


1:	inb %dx, %al
	testb $0x08, %al
	jz 1b



	movl $256, %ecx
	movl $0x1f0, %edx
	rep insw


	incl %ebx
	cmpl $KERNEL_LOAD_PAGES*8 + 1, %ebx
	jnz read_sector


	movl $LOADER_PHYS_BASE, %eax
	call *%eax
	jmp panic


gdt:
	.quad 0x0000000000000000
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff

gdtdesc:
	.word	0x17
	.long	gdt


panic:  .code16
	movw $panic_message, %si
	movb $0xe, %ah
	subb %bh, %bh
1:	lodsb
	test %al, %al
2:	jz 2b
	int $0x10
	jmp 1b

panic_message:
	.ascii "Panic!"
	.byte 0


	.org LOADER_ARG_CNT - LOADER_BASE
arg_cnt:
	.long 0
	.org LOADER_ARGS - LOADER_BASE
args:
	.fill 0x80, 1, 0


	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
