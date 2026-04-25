/* ============================================================
 * thread.c -- 스레드 생성, 스케줄링, 상태 전이
 *
 * Pintos의 스레드 관리 핵심 파일.
 * Project 1에서 가장 많이 수정하는 파일이다.
 *
 * 주요 함수:
 *   thread_init()     - 스레드 시스템 초기화 (main 스레드 등록)
 *   thread_start()    - idle 스레드 생성 + 인터럽트 ON
 *   thread_create()   - 새 커널 스레드 생성
 *   thread_block()    - 현재 스레드를 BLOCKED 상태로 전환
 *   thread_unblock()  - BLOCKED 스레드를 READY로 전환
 *   thread_yield()    - CPU 양보 (READY로 돌아감)
 *   schedule()        - 다음 스레드를 골라 컨텍스트 스위칭
 *
 * Project 1 수정 대상:
 *   Phase 1: init_thread()에 wake_tick 초기화 추가
 *   Phase 2: thread_create(), thread_unblock(), thread_yield()에
 *            우선순위 정렬 + 선점 체크 추가
 *   Phase 3: init_thread()에 donation 필드 초기화,
 *            thread_set_priority()에 donation 고려 로직 추가
 *   Phase 4: MLFQS 계산 함수 추가 (mlfqs_recalc_priority 등)
 * ============================================================ */

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

/* struct thread의 magic 멤버에 넣는 고정 값.
 * 스택 오버플로우가 발생하면 이 값이 덮어쓰여져서
 * is_thread() 검사에서 걸린다.
 * 즉, magic이 이 값이 아니면 스택이 넘친 것이다. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본(초기) 스레드 식별용 값. 수정하지 말 것. */
#define THREAD_BASIC 0xd42df210

/* ============================================================
 * 전역 자료구조
 * ============================================================ */

/* THREAD_READY 상태인 스레드들의 리스트.
 * 실행 준비가 됐지만 CPU를 받지 못한 스레드들이 여기 들어간다.
 *
 * [Phase 2] 이 리스트를 우선순위 내림차순으로 정렬해야 한다.
 * 그래야 next_thread_to_run()에서 list_pop_front()만 해도
 * 최고 우선순위 스레드가 나온다. */
static struct list ready_list;

static struct list sleep_list;
static int64_t next_wakeup_tick;

/* idle 스레드 포인터.
 * ready_list가 비었을 때 CPU를 차지하는 특수 스레드.
 * hlt 명령어로 CPU를 저전력 대기 상태로 만든다. */
static struct thread *idle_thread;

/* 최초 스레드(init.c의 main()을 실행하는 스레드).
 * thread_init()에서 현재 실행 흐름을 이 스레드로 등록한다.
 * 이 스레드는 종료 시 해제하면 안 된다 (커널 스택이므로). */
static struct thread *initial_thread;

/* tid(스레드 ID) 할당에 사용하는 락.
 * 여러 스레드가 동시에 thread_create()를 호출해도
 * tid가 겹치지 않도록 보호한다. */
static struct lock tid_lock;

/* 종료된(THREAD_DYING) 스레드의 해제 대기열.
 * 스레드는 자기 자신의 스택 위에서 실행 중이므로
 * 즉시 해제할 수 없다. schedule()에서 다음 스레드로
 * 전환한 뒤에야 안전하게 페이지를 해제할 수 있다. */
static struct list destruction_req;

/* ============================================================
 * 통계 변수 (print_stats에서 출력)
 * ============================================================ */

/* idle 스레드가 실행된 틱 수.
 * Phase 1 구현 전에는 busy-wait 때문에 항상 0이다.
 * Alarm Clock 구현 후에는 이 값이 크게 증가해야 한다. */
static long long idle_ticks;

/* 커널 스레드가 실행된 틱 수. */
static long long kernel_ticks;

/* 유저 프로그램이 실행된 틱 수. (Project 2에서 사용) */
static long long user_ticks;

/* ============================================================
 * 스케줄링 상수 및 변수
 * ============================================================ */

/* 타임 슬라이스: 한 스레드가 연속으로 실행할 수 있는 최대 틱 수.
 * 4틱(= 40ms)이 지나면 thread_tick()에서 선점을 요청한다.
 * [Phase 4] MLFQS에서 매 4틱마다 우선순위를 재계산하는 주기와 동일. */
#define TIME_SLICE 4

/* 마지막 thread_yield() 이후 경과한 틱 수.
 * TIME_SLICE 이상이 되면 선점이 일어난다. */
static unsigned thread_ticks;

/* MLFQS 스케줄러 활성화 여부.
 * false(기본): 라운드 로빈 + 고정 우선순위 스케줄러.
 * true: MLFQS (다중 레벨 피드백 큐) 스케줄러.
 * 커널 명령줄에서 "-mlfqs" 옵션으로 켠다.
 *
 * true일 때 달라지는 것:
 *   - thread_set_priority()가 무시된다 (스케줄러가 자동 계산)
 *   - priority donation이 비활성화된다
 *   - timer_interrupt()에서 recent_cpu, load_avg, priority를 재계산한다 */
bool thread_mlfqs;

/* ============================================================
 * 내부 함수 전방 선언
 * ============================================================ */
static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 스레드인지 검사하는 매크로.
 * NULL이 아니고 magic 값이 THREAD_MAGIC이면 유효하다.
 * 스택 오버플로우로 magic이 덮어쓰여지면 false가 된다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 현재 CPU에서 실행 중인 스레드를 찾는 매크로.
 *
 * 원리: struct thread는 항상 4KB 페이지의 시작 부분에 위치하고,
 * 스택은 같은 페이지의 끝에서 아래로 자란다.
 * 따라서 스택 포인터(rsp)를 페이지 크기로 내림하면
 * struct thread의 시작 주소가 된다.
 *
 *   +-----------+ <-- 페이지 시작 = struct thread 시작
 *   | thread    |
 *   |  구조체   |
 *   +-----------+
 *   |   ...     |
 *   | 스택 성장 |
 *   |     |     |
 *   |     v     |
 *   |           |
 *   +-----------+ <-- 페이지 끝, 스택 바닥
 */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


/* thread_start()를 위한 임시 GDT(전역 디스크립터 테이블).
 * thread_init()에서 임시로 설정하고,
 * 나중에 gdt_init()에서 유저 컨텍스트를 포함한 정식 GDT로 교체한다.
 *
 * gdt[0]: NULL 디스크립터 (x86 규약상 필수)
 * gdt[1]: 커널 코드 세그먼트 (64비트 모드)
 * gdt[2]: 커널 데이터 세그먼트 */
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* ============================================================
 * thread_init -- 스레드 시스템 초기화
 *
 * init.c의 main()에서 가장 먼저 호출되는 스레드 관련 함수.
 *
 * 하는 일:
 *   1. 임시 GDT를 로드한다.
 *   2. tid_lock, ready_list, destruction_req를 초기화한다.
 *   3. 현재 실행 중인 코드 흐름을 "main" 스레드로 등록한다.
 *
 * 이 함수 호출 전에는 thread_current()를 쓸 수 없다.
 * 이 함수 이후 + palloc_init() 이후에야 thread_create()가 가능하다.
 *
 * 주의: 인터럽트가 꺼진 상태에서 호출되어야 한다.
 * ============================================================ */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* 임시 GDT를 CPU에 로드한다.
	 * 이 GDT에는 유저 컨텍스트가 없다.
	 * 나중에 gdt_init()에서 유저 세그먼트를 포함한 GDT로 교체한다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 전역 자료구조 초기화 */
	lock_init (&tid_lock);       /* tid 할당용 락 */
	list_init (&ready_list);     /* 실행 대기 큐 */
	list_init (&sleep_list);
	next_wakeup_tick = INT64_MAX;
	list_init (&destruction_req); /* 종료 스레드 해제 대기열 */

	/* 현재 실행 흐름을 "main" 스레드로 등록한다.
	 * running_thread() 매크로로 현재 스택 포인터에서 thread 구조체를 찾고,
	 * init_thread()로 이름, 우선순위, magic 등을 설정한다. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* ============================================================
 * thread_start -- 스케줄러 시작
 *
 * 1. idle 스레드를 생성한다.
 * 2. 인터럽트를 ON으로 바꾼다.
 *    -> 이 시점부터 timer_interrupt()가 매 틱마다 호출된다.
 *    -> 선점형 스케줄링이 시작된다.
 * 3. idle 스레드가 초기화를 마칠 때까지 세마포어로 대기한다.
 *
 * idle 스레드가 필요한 이유:
 *   ready_list가 비었을 때 schedule()이 선택할 스레드가 없으면
 *   커널이 멈춘다. idle 스레드가 이때 CPU를 잡아서
 *   hlt 명령으로 저전력 대기 상태를 유지한다.
 * ============================================================ */
void
thread_start (void) {
	/* idle 스레드를 최소 우선순위(PRI_MIN = 0)로 생성한다.
	 * 세마포어를 넘겨서, idle 스레드가 초기화를 완료하면
	 * sema_up()으로 알려주도록 한다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* 인터럽트를 켠다.
	 * 이 순간부터 timer_interrupt()가 주기적으로 호출된다. */
	intr_enable ();

	/* idle 스레드가 idle_thread 전역변수를 설정하고
	 * sema_up()을 호출할 때까지 여기서 대기한다. */
	sema_down (&idle_started);
}

/* ============================================================
 * thread_tick -- 타이머 틱마다 호출되는 통계 + 선점 함수
 *
 * timer_interrupt() -> thread_tick() 순서로 매 틱(10ms)마다 호출된다.
 * 외부 인터럽트 컨텍스트에서 실행되므로 잠들거나 락을 잡으면 안 된다.
 *
 * 하는 일:
 *   1. 현재 스레드 종류에 따라 idle/kernel/user ticks를 증가시킨다.
 *   2. thread_ticks가 TIME_SLICE(4) 이상이면 선점을 요청한다.
 *      -> intr_yield_on_return()은 인터럽트 핸들러가 끝난 뒤
 *         thread_yield()를 호출하도록 예약하는 함수다.
 *
 * [Phase 4] MLFQS에서는 여기에 recent_cpu 증가,
 *           우선순위 재계산 로직을 추가한다.
 * ============================================================ */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* 통계 업데이트: 어떤 종류의 스레드가 CPU를 사용했는지 기록 */
	if (t == idle_thread)
		idle_ticks++;              /* idle 스레드가 돌았다 = CPU가 놀고 있었다 */
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;              /* 유저 프로세스가 돌았다 */
#endif
	else
		kernel_ticks++;            /* 커널 스레드가 돌았다 */

	/* 타임 슬라이스 만료 체크.
	 * thread_ticks를 1 증가시키고, TIME_SLICE(4틱 = 40ms) 이상이면
	 * 이 인터럽트가 끝난 뒤 thread_yield()를 호출하도록 요청한다.
	 * -> 현재 스레드가 ready_list 뒤로 가고, 다른 스레드가 실행된다. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* ============================================================
 * thread_print_stats -- 스레드 실행 통계 출력
 *
 * power_off() -> print_stats()에서 호출된다.
 * "Thread: 0 idle ticks, 581 kernel ticks, 0 user ticks" 형태로 출력.
 *
 * idle_ticks가 0이면 CPU가 한 번도 쉬지 못했다는 뜻이다.
 * Phase 1(Alarm Clock) 구현 전에는 busy-wait 때문에 항상 0이다.
 * ============================================================ */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* ============================================================
 * thread_create -- 새 커널 스레드를 생성하고 ready_list에 넣는다
 *
 * 인자:
 *   name     - 스레드 이름 (디버깅용, 최대 15자)
 *   priority - 초기 우선순위 (PRI_MIN=0 ~ PRI_MAX=63)
 *   function - 스레드가 실행할 함수 포인터
 *   aux      - function에 전달할 인자
 *
 * 반환값:
 *   성공 시 새 스레드의 tid, 실패 시 TID_ERROR
 *
 * 동작 순서:
 *   1. 페이지(4KB) 하나를 할당받아 스레드 구조체 + 스택으로 사용
 *   2. init_thread()로 구조체를 초기화
 *   3. 인터럽트 프레임(tf)에 kernel_thread 함수 주소와 인자를 설정
 *      -> 이 스레드가 처음 스케줄되면 kernel_thread(function, aux) 실행
 *   4. thread_unblock()으로 ready_list에 넣는다
 *
 * [Phase 2] thread_unblock() 이후, 새 스레드의 우선순위가
 *           현재 스레드보다 높으면 즉시 thread_yield()로 선점해야 한다.
 *
 * 메모리 구조:
 *   +-----------+ <-- palloc_get_page()가 반환한 주소
 *   | struct    |
 *   | thread    |     <- 페이지 시작 부분
 *   +-----------+
 *   |           |
 *   |  커널     |
 *   |  스택     |     <- 위에서 아래로 성장
 *   |     |     |
 *   |     v     |
 *   +-----------+ <-- 페이지 끝 (tf.rsp 초기값)
 * ============================================================ */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* 1. 페이지 할당 (4KB, 0으로 초기화) */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 2. 스레드 구조체 초기화 (이름, 우선순위, magic, 스택 포인터 등) */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* 3. 인터럽트 프레임 설정
	 * 이 스레드가 처음 스케줄되면 do_iret()이 이 프레임을 복원하여
	 * kernel_thread(function, aux)를 호출한다.
	 *
	 * rdi = 첫 번째 인자 = function (System V AMD64 호출 규약)
	 * rsi = 두 번째 인자 = aux
	 * rip = 실행 시작 주소 = kernel_thread
	 * eflags = FLAG_IF : 인터럽트 활성화 상태로 시작 */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 4. ready_list에 넣는다 (BLOCKED -> READY 전환) */
	thread_unblock (t);

	/* [Phase 2] 여기에 선점 체크 추가 필요:
	 * if (t->priority > thread_current()->priority)
	 *     thread_yield(); */

	return tid;
}

/* ============================================================
 * thread_block -- 현재 스레드를 BLOCKED 상태로 전환
 *
 * 스레드를 잠들게 한다. thread_unblock()이 호출될 때까지
 * 스케줄링 대상에서 제외된다.
 *
 * 반드시 인터럽트가 꺼진 상태에서 호출해야 한다.
 * 직접 호출보다는 synch.h의 동기화 프리미티브
 * (sema_down, lock_acquire 등)를 통해 간접 호출하는 것이 안전하다.
 *
 * [Phase 1] timer_sleep()에서 thread_block()을 호출하여
 *           스레드를 sleep_list로 보내는 방식으로 busy-wait를 제거한다.
 * ============================================================ */
void
thread_block (void) {
	ASSERT (!intr_context ());           /* 인터럽트 핸들러에서 호출 금지 */
	ASSERT (intr_get_level () == INTR_OFF);  /* 인터럽트가 꺼져있어야 함 */
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();  /* 다른 스레드로 전환. 이 스레드는 unblock될 때까지 여기서 멈춤 */
}

/* ============================================================
 * thread_unblock -- BLOCKED 스레드를 READY 상태로 전환
 *
 * BLOCKED 상태인 스레드 T를 ready_list에 넣고 READY로 바꾼다.
 * T가 BLOCKED가 아니면 ASSERT 실패.
 *
 * 주의: 이 함수는 현재 스레드를 선점하지 않는다.
 * 호출자가 인터럽트를 끈 상태에서 원자적으로 다른 작업과
 * 함께 수행할 수 있도록 의도적으로 선점을 하지 않는다.
 *
 * [Phase 1] timer_interrupt()에서 깨울 때 이 함수를 호출한다.
 * [Phase 2] list_push_back 대신 우선순위 순으로 삽입해야 한다.
 *           list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL)
 * ============================================================ */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);

	/* ready_list 뒤에 넣는다 (FIFO).
	 * [Phase 2] 우선순위 순 삽입으로 변경 필요. */
	list_push_back (&ready_list, &t->elem);
	t->status = THREAD_READY;

	intr_set_level (old_level);
}

/* 현재 실행 중인 스레드의 이름을 반환한다. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* ============================================================
 * thread_current -- 현재 실행 중인 스레드의 포인터를 반환
 *
 * running_thread() 매크로를 호출한 뒤 건전성 검사를 추가한 버전.
 *
 * ASSERT 실패 원인:
 *   1. magic이 THREAD_MAGIC이 아니다 -> 스택 오버플로우
 *      (struct thread는 4KB 페이지의 시작, 스택은 끝에서 자라므로
 *       큰 지역변수나 깊은 재귀로 스택이 넘치면 magic을 덮어쓴다)
 *   2. status가 THREAD_RUNNING이 아니다 -> 내부 로직 버그
 * ============================================================ */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 현재 실행 중인 스레드의 tid를 반환한다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* ============================================================
 * thread_exit -- 현재 스레드를 종료
 *
 * 현재 스레드의 상태를 THREAD_DYING으로 바꾸고
 * schedule()로 다른 스레드를 실행시킨다.
 * 이 함수는 절대 리턴하지 않는다(NOT_REACHED).
 *
 * DYING 상태인 스레드의 메모리는 다음 schedule() 호출 시
 * destruction_req에서 꺼내져서 해제된다.
 * ============================================================ */
void
thread_exit (void) {
	ASSERT (!intr_context ());  /* 인터럽트 핸들러에서 호출 금지 */

#ifdef USERPROG
	process_exit ();
#endif

	/* 인터럽트를 끄고 DYING 상태로 전환한 뒤 스케줄한다.
	 * 이 스레드의 메모리는 다음 스케줄 때 해제된다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* ============================================================
 * thread_yield -- CPU를 양보
 *
 * 현재 스레드를 ready_list에 넣고 다른 스레드를 실행시킨다.
 * BLOCKED가 아니라 READY 상태로 가므로, 곧바로 다시 스케줄될 수 있다.
 *
 * 사용 시점:
 *   - thread_tick()에서 타임 슬라이스 만료 시 (선점)
 *   - timer_sleep()에서 busy-wait 루프 (Phase 1 이전)
 *   - [Phase 2] 우선순위가 더 높은 스레드가 생겼을 때 자발적 양보
 *
 * [Phase 2] list_push_back 대신 우선순위 순으로 삽입해야 한다.
 * ============================================================ */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());  /* 인터럽트 핸들러에서 호출 금지 */

	old_level = intr_disable ();

	/* idle 스레드는 ready_list에 넣지 않는다.
	 * idle은 ready_list가 빌 때만 next_thread_to_run()에서 직접 반환된다. */
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);

	/* 현재 스레드를 READY로 바꾸고 다른 스레드로 전환한다. */
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

void
thread_sleep (int64_t wakeup_tick) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());
	ASSERT (curr != idle_thread);

	old_level = intr_disable ();

	curr->wakeup_tick = wakeup_tick;
	list_insert_ordered (&sleep_list, &curr->elem, wakeup_tick_less, NULL);
	if (wakeup_tick < next_wakeup_tick)
		next_wakeup_tick = wakeup_tick;
	thread_block ();

	intr_set_level (old_level);
}

void
thread_awake (int64_t current_tick) {
	if (current_tick < next_wakeup_tick)
		return;

	while (!list_empty (&sleep_list)) {
		struct list_elem *front = list_front (&sleep_list);
		struct thread *sleeper = list_entry (front, struct thread, elem);

		if (sleeper->wakeup_tick > current_tick)
			break;

		list_pop_front (&sleep_list);
		thread_unblock (sleeper);
	}

	if (list_empty (&sleep_list))
		next_wakeup_tick = INT64_MAX;
	else
		next_wakeup_tick = list_entry (list_front (&sleep_list),
		                               struct thread, elem)->wakeup_tick;
}

/* ============================================================
 * thread_set_priority -- 현재 스레드의 우선순위를 변경
 *
 * [Phase 2] 우선순위 변경 후, ready_list에 자신보다 높은
 *           우선순위 스레드가 있으면 thread_yield()로 양보해야 한다.
 *
 * [Phase 3] priority donation이 있을 때:
 *           new_priority를 original_priority에 저장하고,
 *           기부받은 우선순위와 비교하여 더 높은 값을 사용해야 한다.
 *
 * [Phase 4] MLFQS 모드(thread_mlfqs == true)에서는
 *           이 함수가 아무 일도 하지 않아야 한다.
 *           스케줄러가 우선순위를 자동 계산하기 때문이다.
 * ============================================================ */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
}

/* 현재 스레드의 우선순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* ============================================================
 * MLFQS 관련 함수 (Phase 4에서 구현)
 *
 * 아래 4개 함수는 MLFQS 스케줄러에서 사용하는 값을 설정/반환한다.
 * Phase 4 구현 전까지는 빈 껍데기 상태다.
 * ============================================================ */

/* 현재 스레드의 nice 값을 설정한다.
 * nice: -20(공격적) ~ 20(양보적).
 * nice가 높을수록 priority가 낮아져서 CPU를 덜 받는다.
 *
 * [Phase 4] nice 변경 후 우선순위를 재계산하고,
 *           필요하면 thread_yield()로 양보해야 한다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Phase 4에서 구현 */
}

/* 현재 스레드의 nice 값을 반환한다. */
int
thread_get_nice (void) {
	/* TODO: Phase 4에서 구현 */
	return 0;
}

/* 시스템 load_avg의 100배를 정수로 반환한다.
 * load_avg: 최근 1분간 ready_list의 평균 스레드 수.
 * 실제 값은 fixed-point(17.14 형식)이므로
 * 100을 곱하고 정수로 변환하여 반환한다. */
int
thread_get_load_avg (void) {
	/* TODO: Phase 4에서 구현 */
	return 0;
}

/* 현재 스레드의 recent_cpu 값의 100배를 정수로 반환한다.
 * recent_cpu: 이 스레드가 최근에 사용한 CPU 시간의 추정치.
 * 값이 클수록 priority가 낮아진다. */
int
thread_get_recent_cpu (void) {
	/* TODO: Phase 4에서 구현 */
	return 0;
}

/* ============================================================
 * idle -- idle 스레드의 본체
 *
 * ready_list가 비었을 때 CPU를 차지하는 특수 스레드.
 *
 * 동작:
 *   1. idle_thread 전역변수에 자기 자신을 등록한다.
 *   2. sema_up()으로 thread_start()에게 초기화 완료를 알린다.
 *   3. 무한 루프: thread_block()으로 자신을 BLOCKED로 만든 뒤,
 *      sti; hlt 명령어로 인터럽트를 켜고 CPU를 멈춘다.
 *
 * sti와 hlt가 원자적으로 실행되는 이유:
 *   x86의 sti 명령어는 "다음 명령어가 끝날 때까지" 인터럽트를
 *   비활성화 상태로 유지한다. 따라서 sti 직후의 hlt가 실행되기 전에
 *   인터럽트가 끼어들 수 없다.
 *   만약 sti와 hlt 사이에 인터럽트가 끼어들면,
 *   인터럽트 처리 후 hlt가 실행되어 다음 인터럽트까지
 *   최대 1틱(10ms)을 낭비할 수 있다.
 *
 * idle 스레드는 ready_list에 들어가지 않는다.
 * next_thread_to_run()에서 ready_list가 비면 직접 idle_thread를 반환한다.
 * ============================================================ */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* BLOCKED로 전환 -> schedule()이 다른 스레드를 실행한다.
		 * 다른 스레드가 없으면 여기로 돌아와서 아래 hlt를 실행한다. */
		intr_disable ();
		thread_block ();

		/* 인터럽트를 켜고(sti) CPU를 멈춘다(hlt).
		 * 다음 인터럽트(주로 timer_interrupt)가 오면 깨어난다. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* ============================================================
 * kernel_thread -- 커널 스레드의 진입점
 *
 * thread_create()에서 tf.rip에 이 함수 주소를 넣어둔다.
 * 스레드가 처음 스케줄되면 do_iret()이 인터럽트 프레임을 복원하여
 * 이 함수를 호출한다.
 *
 * 동작:
 *   1. 인터럽트를 켠다 (schedule()은 인터럽트 OFF 상태로 전환하므로)
 *   2. function(aux)를 실행한다 (스레드의 실제 작업)
 *   3. function()이 리턴하면 thread_exit()로 스레드를 종료한다
 * ============================================================ */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* 스케줄러가 인터럽트를 끄고 오므로 다시 켠다 */
	function (aux);       /* 스레드의 실제 작업 실행 */
	thread_exit ();       /* function()이 리턴하면 스레드 종료 */
}


/* ============================================================
 * init_thread -- 스레드 구조체의 기본 초기화
 *
 * palloc_get_page()로 할당받은 페이지를 스레드로 세팅한다.
 * 상태는 THREAD_BLOCKED으로 시작한다 (thread_unblock()으로 READY 전환).
 *
 * 초기화 항목:
 *   - 구조체 전체를 0으로 밀기 (memset)
 *   - status = BLOCKED
 *   - name = 스레드 이름 (최대 15자 + NULL)
 *   - tf.rsp = 페이지 끝 (스택 바닥)
 *   - priority = 전달받은 우선순위
 *   - magic = THREAD_MAGIC (스택 오버플로우 감지용)
 *
 * [Phase 1] 여기에 wake_tick = 0 초기화 추가
 * [Phase 3] 여기에 original_priority, wait_on_lock,
 *           donations 리스트 초기화 추가
 * [Phase 4] 여기에 nice = 0, recent_cpu = 0 초기화 추가
 * ============================================================ */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);         /* 구조체 전체를 0으로 초기화 */
	t->status = THREAD_BLOCKED;       /* 초기 상태는 BLOCKED */
	strlcpy (t->name, name, sizeof t->name);  /* 이름 복사 (최대 15자) */

	/* 스택 포인터를 페이지 끝으로 설정한다.
	 * void* 하나만큼 빼는 이유: x86-64 호출 규약에서
	 * 함수 진입 시 rsp가 16바이트 정렬에서 8바이트 어긋나야 하기 때문이다.
	 * (CALL 명령이 리턴 주소 8바이트를 push하므로) */
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);

	t->priority = priority;           /* 우선순위 설정 */
	t->magic = THREAD_MAGIC;          /* 스택 오버플로우 감지용 매직 넘버 */
}

/* ============================================================
 * next_thread_to_run -- ready_list에서 다음 실행할 스레드를 꺼낸다
 *
 * ready_list가 비어있으면 idle_thread를 반환한다.
 * 비어있지 않으면 맨 앞 원소를 pop하여 반환한다.
 *
 * [Phase 2] ready_list가 우선순위 내림차순으로 정렬되어 있으면
 *           list_pop_front()만으로 최고 우선순위 스레드를 꺼낼 수 있다.
 *           정렬하지 않았다면 여기서 list_max()로 찾아야 한다.
 * ============================================================ */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* ============================================================
 * do_iret -- 인터럽트 프레임을 복원하여 스레드를 실행
 *
 * 인터럽트 리턴(iretq) 명령어를 사용하여 tf에 저장된
 * 레지스터 값들을 CPU에 복원한다.
 *
 * 복원 순서:
 *   1. rsp를 tf의 시작 주소로 설정
 *   2. r15~rax를 순서대로 복원
 *   3. ds, es 세그먼트 레지스터 복원
 *   4. iretq 실행: rip, cs, eflags, rsp, ss를 한번에 복원
 *      -> 이 시점에서 새 스레드의 코드가 실행되기 시작한다
 * ============================================================ */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"          /* rsp = tf 시작 주소 */
			"movq 0(%%rsp),%%r15\n"     /* 범용 레지스터 복원 */
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
			"addq $120,%%rsp\n"         /* 범용 레지스터 영역 건너뛰기 */
			"movw 8(%%rsp),%%ds\n"      /* 세그먼트 레지스터 복원 */
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"         /* 세그먼트 영역 건너뛰기 */
			"iretq"                     /* rip, cs, eflags, rsp, ss 복원 */
			: : "g" ((uint64_t) tf) : "memory");
}

/* ============================================================
 * thread_launch -- 컨텍스트 스위칭 실행
 *
 * 현재 스레드의 레지스터를 인터럽트 프레임(tf)에 저장하고,
 * 새 스레드 th의 인터럽트 프레임을 do_iret()으로 복원한다.
 *
 * 이 함수가 호출되는 시점:
 *   - schedule()에서 curr != next일 때
 *   - 인터럽트는 반드시 OFF 상태여야 한다
 *
 * 핵심 원리:
 *   현재 레지스터 상태를 tf_cur에 저장해두면,
 *   나중에 이 스레드가 다시 스케줄될 때 do_iret()이
 *   tf_cur를 복원하여 "out_iret:" 레이블부터 실행을 재개한다.
 *
 * 주의: 이 함수 내부에서는 스택(지역변수)을 사용하면 안 된다.
 *       컨텍스트 스위칭 도중 스택이 바뀌기 때문이다.
 * ============================================================ */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* 레지스터 저장 및 스위칭 로직 (인라인 어셈블리).
	 *
	 * 1단계: 현재 레지스터를 tf_cur(현재 스레드의 인터럽트 프레임)에 저장
	 * 2단계: rip를 "out_iret" 레이블 주소로 설정
	 *        -> 이 스레드가 다시 돌아오면 out_iret부터 실행
	 * 3단계: do_iret(tf)를 호출하여 새 스레드로 전환
	 *        -> 새 스레드의 레지스터가 복원되고 실행 시작
	 */
	__asm __volatile (
			/* 사용할 레지스터를 임시 보관 */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* 입력값 로드 */
			"movq %0, %%rax\n"          /* rax = tf_cur (저장할 위치) */
			"movq %1, %%rcx\n"          /* rcx = tf (복원할 위치) */
			/* 현재 레지스터를 tf_cur에 저장 */
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
			"pop %%rbx\n"              /* 저장했던 rcx 복원 */
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              /* 저장했던 rbx 복원 */
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              /* 저장했던 rax 복원 */
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"       /* 세그먼트 레지스터 영역으로 이동 */
			"movw %%es, (%%rax)\n"     /* es 저장 */
			"movw %%ds, 8(%%rax)\n"    /* ds 저장 */
			"addq $32, %%rax\n"        /* iret 프레임 영역으로 이동 */
			"call __next\n"            /* 현재 rip를 스택에 push */
			"__next:\n"
			"pop %%rbx\n"              /* rbx = 현재 rip (__next 주소) */
			"addq $(out_iret -  __next), %%rbx\n"  /* rbx = out_iret 주소 */
			"movq %%rbx, 0(%%rax)\n"   /* rip = out_iret (복귀 지점) */
			"movw %%cs, 8(%%rax)\n"    /* cs 저장 */
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n"   /* eflags 저장 */
			"mov %%rsp, 24(%%rax)\n"   /* rsp 저장 */
			"movw %%ss, 32(%%rax)\n"   /* ss 저장 */
			"mov %%rcx, %%rdi\n"       /* rdi = tf (do_iret의 인자) */
			"call do_iret\n"           /* 새 스레드로 전환! */
			"out_iret:\n"              /* <- 이 스레드가 다시 돌아오는 지점 */
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* ============================================================
 * do_schedule -- 현재 스레드의 상태를 바꾸고 스케줄링
 *
 * 인자 status: 현재 스레드에 설정할 새 상태
 *   - THREAD_READY: thread_yield()에서 호출 (CPU 양보)
 *   - THREAD_DYING: thread_exit()에서 호출 (스레드 종료)
 *   - THREAD_BLOCKED: thread_block()에서는 직접 status를 바꾸고
 *     schedule()을 호출하므로 여기를 거치지 않는다.
 *
 * destruction_req 처리:
 *   이전 스케줄에서 DYING 상태로 등록된 스레드의 페이지를 해제한다.
 *   스레드는 자기 스택 위에서 실행 중이므로 즉시 해제할 수 없고,
 *   다음 스레드로 전환된 이후에야 안전하게 해제할 수 있다.
 * ============================================================ */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);

	/* 이전에 종료된 스레드의 메모리를 해제한다. */
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);  /* 4KB 페이지 반환 */
	}

	/* 현재 스레드의 상태를 변경하고 다른 스레드로 전환한다. */
	thread_current ()->status = status;
	schedule ();
}

/* ============================================================
 * schedule -- 다음 스레드를 선택하고 컨텍스트 스위칭
 *
 * Pintos 스케줄링의 핵심 함수.
 *
 * 동작:
 *   1. next_thread_to_run()으로 다음 스레드를 선택한다.
 *   2. next의 상태를 RUNNING으로 바꾼다.
 *   3. thread_ticks를 0으로 리셋한다 (새 타임 슬라이스 시작).
 *   4. curr != next이면 thread_launch()로 컨텍스트 스위칭한다.
 *      curr == next이면 아무 일도 안 한다 (같은 스레드 계속 실행).
 *
 * DYING 스레드 처리:
 *   컨텍스트 스위칭 전에 curr가 DYING이면 destruction_req에 넣는다.
 *   실제 해제는 다음 do_schedule() 호출 시 수행된다.
 *   initial_thread(main)는 해제하면 안 된다.
 *
 * 주의: 이 함수 내에서 printf()를 호출하면 안 된다.
 *       컨텍스트 스위칭이 완료되기 전에 콘솔 락을 잡으면 데드락이 난다.
 * ============================================================ */
static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));

	/* 다음 스레드를 RUNNING으로 표시한다. */
	next->status = THREAD_RUNNING;

	/* 새 타임 슬라이스를 시작한다.
	 * thread_tick()에서 이 값이 TIME_SLICE에 도달하면 선점한다. */
	thread_ticks = 0;

#ifdef USERPROG
	/* 유저 프로세스의 페이지 테이블을 활성화한다. (Project 2) */
	process_activate (next);
#endif

	if (curr != next) {
		/* 현재 스레드가 DYING이면 해제 대기열에 등록한다.
		 * 단, initial_thread(main)는 해제하면 안 된다.
		 * 이 시점에서 curr의 스택은 아직 사용 중이므로
		 * 즉시 해제하지 않고 대기열에만 넣는다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* 컨텍스트 스위칭!
		 * 현재 레지스터를 curr의 tf에 저장하고,
		 * next의 tf를 복원하여 next의 코드를 실행한다. */
		thread_launch (next);
	}
}

/* ============================================================
 * allocate_tid -- 새 스레드에 고유한 tid를 할당
 *
 * 정적 변수 next_tid를 1씩 증가시키며 반환한다.
 * tid_lock으로 보호하여 동시 호출 시에도 중복되지 않는다.
 * ============================================================ */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
