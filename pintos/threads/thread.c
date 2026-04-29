

#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif


#define THREAD_MAGIC 0xcd6abf4b


#define THREAD_BASIC 0xd42df210




static struct list ready_list;

static struct sleep_queue sleep_q;


static struct thread *idle_thread;


static struct thread *initial_thread;


static struct lock tid_lock;


static struct list destruction_req;




static long long idle_ticks;


static long long kernel_ticks;


static long long user_ticks;




#define TIME_SLICE 4


static unsigned thread_ticks;


bool thread_mlfqs;


static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

static void ready_list_push (struct thread *);
static struct thread *ready_list_pop (void);
static void sleepers_push (struct thread *);
static void sleepers_refresh_tick (void);

#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)


#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))



static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };


void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);


	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);


	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_q.sleepers);
	sleep_q.next_tick = INT64_MAX;
	list_init (&destruction_req);



	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}


void
thread_start (void) {

	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);


	intr_enable ();


	sema_down (&idle_started);
}


void
thread_tick (void) {
	struct thread *t = thread_current ();



	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;


	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}


void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}


tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);


	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;


	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();


	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;


	thread_unblock (t);
	check_preemption ();

	return tid;
}


void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);



	thread_current()->status = THREAD_BLOCKED;



	schedule ();
}


void
thread_unblock (struct thread *t)
{
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);



	ready_list_push (t);
	t->status = THREAD_READY;

	intr_set_level (old_level);
}


const char *
thread_name (void) {
	return thread_current ()->name;
}


struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}


tid_t
thread_tid (void) {
	return thread_current ()->tid;
}


void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif


	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}


void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();


	if (curr != idle_thread)
		ready_list_push (curr);


	do_schedule (THREAD_READY);


	intr_set_level (old_level);
}

static bool
wakeup_tick_less (const struct list_elem *lhs,
                  const struct list_elem *rhs,
                  void *aux UNUSED) {
	const struct thread *lhs_thread = list_entry (lhs, struct thread, elem);
	const struct thread *rhs_thread = list_entry (rhs, struct thread, elem);
	return lhs_thread->wakeup_tick < rhs_thread->wakeup_tick;
}

static bool
priority_greater (const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux UNUSED) {
	return list_entry (a, struct thread, elem)->priority
	     > list_entry (b, struct thread, elem)->priority;
}

static void
ready_list_push (struct thread *t) {
	list_insert_ordered (&ready_list, &t->elem, priority_greater, NULL);
}

static struct thread *
ready_list_pop (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

void
check_preemption (void) {
	if (list_empty (&ready_list))
		return;

	struct thread *cur = thread_current ();
	struct thread *front = list_entry (list_front (&ready_list),
	                                   struct thread, elem);
	if (front->priority > cur->priority) {
		if (intr_context ())
			intr_yield_on_return ();
		else
			thread_yield ();
	}
}

static void
sleepers_push (struct thread *t) {
	list_insert_ordered (&sleep_q.sleepers, &t->elem, wakeup_tick_less, NULL);
	if (t->wakeup_tick < sleep_q.next_tick)
		sleep_q.next_tick = t->wakeup_tick;
}

static void
sleepers_refresh_tick (void) {
	if (list_empty (&sleep_q.sleepers))
		sleep_q.next_tick = INT64_MAX;
	else
		sleep_q.next_tick = list_entry (list_front (&sleep_q.sleepers),
		                                struct thread, elem)->wakeup_tick;
}

void
thread_sleep (int64_t wakeup_tick) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());
	ASSERT (curr != idle_thread);

	old_level = intr_disable ();

	curr->wakeup_tick = wakeup_tick;
	sleepers_push (curr);
	thread_block ();

	intr_set_level (old_level);
}

void
thread_awake (int64_t current_tick) {
	if (current_tick < sleep_q.next_tick)
		return;

	while (!list_empty (&sleep_q.sleepers)) {
		struct list_elem *front = list_front (&sleep_q.sleepers);
		struct thread *sleeper = list_entry (front, struct thread, elem);

		if (sleeper->wakeup_tick > current_tick)
			break;

		list_pop_front (&sleep_q.sleepers);
		thread_unblock (sleeper);
	}

	sleepers_refresh_tick ();
}


void
thread_set_priority (int new_priority) {
  thread_current()->priority = new_priority;
  check_preemption();
}


int
thread_get_priority (void) {
	return thread_current ()->priority;
}




void
thread_set_nice (int nice UNUSED) {

}


int
thread_get_nice (void) {

	return 0;
}


int
thread_get_load_avg (void) {

	return 0;
}


int
thread_get_recent_cpu (void) {

	return 0;
}


static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {

		intr_disable ();
		thread_block ();


		asm volatile ("sti; hlt" : : : "memory");
	}
}


static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();
	function (aux);
	thread_exit ();
}



static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);


	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);

	t->priority = priority;
	t->magic = THREAD_MAGIC;
}


static struct thread *
next_thread_to_run (void) {
	return ready_list_pop ();
}


void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}


static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);


	__asm __volatile (

			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"

			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"

			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n"
			"movw %%cs, 8(%%rax)\n"
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n"
			"mov %%rsp, 24(%%rax)\n"
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}


static void
do_schedule(int status) {

	ASSERT (intr_get_level () == INTR_OFF);


	ASSERT (thread_current()->status == THREAD_RUNNING);


	while (!list_empty (&destruction_req)) {

		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}


	thread_current ()->status = status;
	schedule ();
}


static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));


	next->status = THREAD_RUNNING;


	thread_ticks = 0;

#ifdef USERPROG

	process_activate (next);
#endif

	if (curr != next) {

		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}


		thread_launch (next);
	}
}


static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
