

#include "threads/interrupt.h"
#include <debug.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "threads/flags.h"
#include "threads/intr-stubs.h"
#include "threads/io.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/gdt.h"
#endif


#define INTR_CNT 256


struct gate {
	unsigned off_15_0 : 16;
	unsigned ss : 16;
	unsigned ist : 3;
	unsigned rsv1 : 5;
	unsigned type : 4;
	unsigned s : 1;
	unsigned dpl : 2;
	unsigned p : 1;
	unsigned off_31_16 : 16;
	uint32_t off_32_63;
	uint32_t rsv2;
};


static struct gate idt[INTR_CNT];


static struct desc_ptr idt_desc = {
	.size = sizeof(idt) - 1,
	.address = (uint64_t) idt
};


#define make_gate(g, function, d, t) \
{ \
	ASSERT ((function) != NULL); \
	ASSERT ((d) >= 0 && (d) <= 3); \
	ASSERT ((t) >= 0 && (t) <= 15); \
	*(g) = (struct gate) { \
		.off_15_0 = (uint64_t) (function) & 0xffff, \
		.ss = SEL_KCSEG, \
		.ist = 0, \
		.rsv1 = 0, \
		.type = (t), \
		.s = 0, \
		.dpl = (d), \
		.p = 1, \
		.off_31_16 = ((uint64_t) (function) >> 16) & 0xffff, \
		.off_32_63 = ((uint64_t) (function) >> 32) & 0xffffffff, \
		.rsv2 = 0, \
	}; \
}


#define make_intr_gate(g, function, dpl) make_gate((g), (function), (dpl), 14)


#define make_trap_gate(g, function, dpl) make_gate((g), (function), (dpl), 15)



static intr_handler_func *intr_handlers[INTR_CNT];


static const char *intr_names[INTR_CNT];


static bool in_external_intr;
static bool yield_on_return;


static void pic_init (void);
static void pic_end_of_interrupt (int irq);


void intr_handler (struct intr_frame *args);


enum intr_level
intr_get_level (void) {
	uint64_t flags;

	asm volatile ("pushfq; popq %0" : "=g" (flags));

	return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}


enum intr_level
intr_set_level (enum intr_level level) {
	return level == INTR_ON ? intr_enable () : intr_disable ();
}


enum intr_level
intr_enable (void) {
	enum intr_level old_level = intr_get_level ();
	ASSERT (!intr_context ());

	asm volatile ("sti");

	return old_level;
}


enum intr_level
intr_disable (void) {
	enum intr_level old_level = intr_get_level ();

	asm volatile ("cli" : : : "memory");

	return old_level;
}


void
intr_init (void) {
	int i;


	pic_init ();


	for (i = 0; i < INTR_CNT; i++) {
		make_intr_gate(&idt[i], intr_stubs[i], 0);
		intr_names[i] = "unknown";
	}

#ifdef USERPROG

	ltr (SEL_TSS);
#endif


	lidt(&idt_desc);


	intr_names[0] = "#DE Divide Error";
	intr_names[1] = "#DB Debug Exception";
	intr_names[2] = "NMI Interrupt";
	intr_names[3] = "#BP Breakpoint Exception";
	intr_names[4] = "#OF Overflow Exception";
	intr_names[5] = "#BR BOUND Range Exceeded Exception";
	intr_names[6] = "#UD Invalid Opcode Exception";
	intr_names[7] = "#NM Device Not Available Exception";
	intr_names[8] = "#DF Double Fault Exception";
	intr_names[9] = "Coprocessor Segment Overrun";
	intr_names[10] = "#TS Invalid TSS Exception";
	intr_names[11] = "#NP Segment Not Present";
	intr_names[12] = "#SS Stack Fault Exception";
	intr_names[13] = "#GP General Protection Exception";
	intr_names[14] = "#PF Page-Fault Exception";
	intr_names[16] = "#MF x87 FPU Floating-Point Error";
	intr_names[17] = "#AC Alignment Check Exception";
	intr_names[18] = "#MC Machine-Check Exception";
	intr_names[19] = "#XF SIMD Floating-Point Exception";
}


static void
register_handler (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name) {
	ASSERT (intr_handlers[vec_no] == NULL);
	if (level == INTR_ON) {
		make_trap_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	else {
		make_intr_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	intr_handlers[vec_no] = handler;
	intr_names[vec_no] = name;
}


void
intr_register_ext (uint8_t vec_no, intr_handler_func *handler,
		const char *name) {
	ASSERT (vec_no >= 0x20 && vec_no <= 0x2f);
	register_handler (vec_no, 0, INTR_OFF, handler, name);
}


void
intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name)
{
	ASSERT (vec_no < 0x20 || vec_no > 0x2f);
	register_handler (vec_no, dpl, level, handler, name);
}


bool
intr_context (void) {
	return in_external_intr;
}


void
intr_yield_on_return (void) {
	ASSERT (intr_context ());
	yield_on_return = true;
}




static void
pic_init (void) {

	outb (0x21, 0xff);
	outb (0xa1, 0xff);


	outb (0x20, 0x11);
	outb (0x21, 0x20);
	outb (0x21, 0x04);
	outb (0x21, 0x01);


	outb (0xa0, 0x11);
	outb (0xa1, 0x28);
	outb (0xa1, 0x02);
	outb (0xa1, 0x01);


	outb (0x21, 0x00);
	outb (0xa1, 0x00);
}


static void
pic_end_of_interrupt (int irq) {
	ASSERT (irq >= 0x20 && irq < 0x30);


	outb (0x20, 0x20);


	if (irq >= 0x28)
		outb (0xa0, 0x20);
}


void
intr_handler (struct intr_frame *frame) {
	bool external;
	intr_handler_func *handler;


	external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;

	if (external) {

		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (!intr_context ());

		in_external_intr = true;
		yield_on_return = false;
	}


	handler = intr_handlers[frame->vec_no];
	if (handler != NULL)
		handler (frame);
	else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {

	} else {

		intr_dump_frame (frame);
		PANIC ("Unexpected interrupt");
	}


	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (intr_context ());

		in_external_intr = false;
		pic_end_of_interrupt (frame->vec_no);


		if (yield_on_return)
			thread_yield ();
	}
}


void
intr_dump_frame (const struct intr_frame *f) {
	uint64_t cr2 = rcr2();
	printf ("Interrupt %#04llx (%s) at rip=%llx\n",
			f->vec_no, intr_names[f->vec_no], f->rip);
	printf (" cr2=%016llx error=%16llx\n", cr2, f->error_code);
	printf ("rax %016llx rbx %016llx rcx %016llx rdx %016llx\n",
			f->R.rax, f->R.rbx, f->R.rcx, f->R.rdx);
	printf ("rsp %016llx rbp %016llx rsi %016llx rdi %016llx\n",
			f->rsp, f->R.rbp, f->R.rsi, f->R.rdi);
	printf ("rip %016llx r8 %016llx  r9 %016llx r10 %016llx\n",
			f->rip, f->R.r8, f->R.r9, f->R.r10);
	printf ("r11 %016llx r12 %016llx r13 %016llx r14 %016llx\n",
			f->R.r11, f->R.r12, f->R.r13, f->R.r14);
	printf ("r15 %016llx rflags %08llx\n", f->R.r15, f->eflags);
	printf ("es: %04x ds: %04x cs: %04x ss: %04x\n",
			f->es, f->ds, f->cs, f->ss);
}


const char *
intr_name (uint8_t vec) {
	return intr_names[vec];
}
