/* ============================================================
 * init.c -- Pintos 커널 진입점
 *
 * QEMU가 부트로더(loader.S)를 실행하면, 부트로더가 커널을
 * 메모리에 올리고 start.S를 거쳐 이 파일의 main()을 호출한다.
 *
 * main()은 OS의 모든 서브시스템을 순서대로 초기화한 뒤,
 * 커널 명령줄에 지정된 테스트(예: alarm-single)를 실행한다.
 * ============================================================ */

#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#endif
#include "tests/threads/tests.h"
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef FILESYS
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* ------------------------------------------------------------
 * 전역 변수
 * ------------------------------------------------------------ */

/* 커널 전용 페이지 테이블의 최상위(PML4).
 * x86-64는 4단계 페이지 테이블을 사용한다.
 * CR3 레지스터가 이 주소를 가리키면 CPU가 가상 주소를
 * 물리 주소로 변환할 수 있다. */
uint64_t *base_pml4;

#ifdef FILESYS
/* -f 옵션: 부팅 시 파일 시스템을 포맷할지 여부 */
static bool format_filesys;
#endif

/* -q 옵션: 테스트 실행 후 QEMU를 자동 종료할지 여부.
 * pintos -- -q run alarm-single 에서 -q가 이 값을 true로 만든다. */
bool power_off_when_done;

/* 스레드 테스트 모드 여부 (USERPROG에서 사용) */
bool thread_tests;

/* ------------------------------------------------------------
 * 함수 전방 선언
 * ------------------------------------------------------------ */
static void bss_init (void);
static void paging_init (uint64_t mem_end);

static char **read_command_line (void);
static char **parse_options (char **argv);
static void run_actions (char **argv);
static void usage (void);

static void print_stats (void);


/* main()은 절대 리턴하지 않는다. 마지막에 thread_exit()로 종료. */
int main (void) NO_RETURN;

/* ============================================================
 * main -- Pintos 커널의 메인 함수
 *
 * 부팅 순서:
 *   1) BSS 초기화 + 명령줄 파싱
 *   2) 스레드 시스템 + 콘솔
 *   3) 메모리 시스템 (페이지 할당, malloc, 페이지 테이블)
 *   4) 인터럽트 핸들러 + 디바이스 드라이버
 *   5) 스케줄러 시작 (인터럽트 ON)
 *   6) "Boot complete." 출력 후 테스트 실행
 *   7) 종료
 * ============================================================ */
int
main (void) {
	uint64_t mem_end;   /* RAM의 끝 주소 (바이트) */
	char **argv;        /* 커널 명령줄 인자 배열 */

	/* ---- 1단계: BSS 초기화 + 명령줄 파싱 ---- */

	/* BSS 영역(초기값 없는 전역변수)을 0으로 채운다.
	 * 이게 가장 먼저인 이유: 전역변수가 쓰레기값이면 이후 코드가 오동작한다. */
	bss_init ();

	/* 부트로더가 메모리에 저장해 둔 커널 명령줄을 읽어서
	 * argv 배열로 만든다.
	 * 예: "pintos -- -q run alarm-single"
	 *   -> argv = ["-q", "run", "alarm-single", NULL] */
	argv = read_command_line ();

	/* argv에서 '-'로 시작하는 옵션을 처리하고,
	 * 옵션이 아닌 첫 번째 인자(예: "run")를 가리키는 포인터를 반환한다. */
	argv = parse_options (argv);

	/* ---- 2단계: 스레드 시스템 + 콘솔 ---- */

	/* 현재 실행 중인 코드 흐름을 "main" 스레드로 등록한다.
	 * 이 시점부터 thread_current()가 동작하고, 락을 사용할 수 있다.
	 * ready_list, all_list 등 스케줄러 자료구조도 여기서 초기화된다. */
	thread_init ();

	/* 콘솔(printf) 출력에 락을 건다.
	 * thread_init() 이후여야 락을 쓸 수 있으므로 이 순서다. */
	console_init ();

	/* ---- 3단계: 메모리 시스템 ---- */

	/* 물리 메모리를 페이지(4KB) 단위로 관리하는 할당기를 초기화한다.
	 * 사용 가능한 RAM의 끝 주소를 반환한다. */
	mem_end = palloc_init ();

	/* palloc 위에 바이트 단위 할당기(malloc/free)를 구축한다. */
	malloc_init ();

	/* 커널 페이지 테이블(PML4)을 만들어서 물리 메모리 전체를
	 * 커널 가상 주소 공간에 매핑한다. CR3에 등록하면 CPU가 사용한다. */
	paging_init (mem_end);

#ifdef USERPROG
	/* Task State Segment 초기화 -- 유저모드 -> 커널모드 전환 시
	 * CPU가 사용할 커널 스택 주소를 TSS에 기록한다. */
	tss_init ();
	/* Global Descriptor Table 초기화 -- 세그먼트 디스크립터 설정 */
	gdt_init ();
#endif

	/* ---- 4단계: 인터럽트 핸들러 + 디바이스 ---- */

	/* IDT(Interrupt Descriptor Table) 설정.
	 * 각 인터럽트 번호에 대응하는 핸들러 함수 주소를 등록한다. */
	intr_init ();

	/* 타이머 인터럽트 핸들러(timer_interrupt)를 IDT에 등록한다.
	 * 8254 PIT를 TIMER_FREQ(100Hz)로 설정하여
	 * 초당 100번 인터럽트가 발생하게 한다.
	 * --> Project 1의 핵심 진입점. timer_interrupt()가 매 틱마다 호출된다. */
	timer_init ();

	/* 키보드 인터럽트 핸들러 등록 */
	kbd_init ();

	/* 입력 버퍼 초기화 (키보드, 시리얼 입력을 큐에 저장) */
	input_init ();

#ifdef USERPROG
	/* 유저 프로그램의 예외(page fault 등) 핸들러 등록 */
	exception_init ();
	/* 시스템 콜 핸들러 등록 */
	syscall_init ();
#endif

	/* ---- 5단계: 스케줄러 시작 ---- */

	/* idle 스레드를 생성하고 인터럽트를 ON으로 바꾼다.
	 * 이 시점부터:
	 *   - timer_interrupt()가 매 10ms(1틱)마다 호출된다.
	 *   - thread_tick()이 선점 여부를 판단한다.
	 *   - ready_list에 스레드가 있으면 스케줄링이 일어난다.
	 * idle 스레드는 ready_list가 비었을 때 CPU를 점유하는 역할이다. */
	thread_start ();

	/* 시리얼 포트를 폴링 모드에서 인터럽트 모드로 전환한다.
	 * 인터럽트가 켜진 이후에만 가능하므로 thread_start() 다음이다. */
	serial_init_queue ();

	/* CPU의 반복문 실행 속도를 측정하여 busy-wait 시간을 보정한다.
	 * timer_calibrate()가 출력하는 "xxx loops/s" 가 이 결과다. */
	timer_calibrate ();

#ifdef FILESYS
	/* 디스크 디바이스 초기화 */
	disk_init ();
	/* 파일 시스템 초기화. -f 옵션이면 디스크를 포맷한다. */
	filesys_init (format_filesys);
#endif

#ifdef VM
	/* 가상 메모리 서브시스템 초기화 (Project 3에서 사용) */
	vm_init ();
#endif

	/* ---- 6단계: 부팅 완료 ---- */
	printf ("Boot complete.\n");

	/* 커널 명령줄에 지정된 동작을 실행한다.
	 * "run alarm-single" -> run_task() -> run_test("alarm-single")
	 * 이 호출이 테스트 함수를 실제로 실행하는 부분이다. */
	run_actions (argv);

	/* ---- 7단계: 종료 ---- */

	/* -q 옵션이 있었으면 QEMU를 꺼서 터미널로 돌아간다. */
	if (power_off_when_done)
		power_off ();

	/* main 스레드를 종료한다. 리턴하지 않고 여기서 스레드가 죽는다. */
	thread_exit ();
}

/* ============================================================
 * bss_init -- BSS 영역을 0으로 초기화
 *
 * BSS(Block Started by Symbol)는 초기값이 지정되지 않은
 * 전역변수/정적변수가 모이는 메모리 영역이다.
 *
 * 일반 OS에서는 로더가 BSS를 0으로 채워주지만,
 * Pintos의 부트로더는 이 작업을 안 하므로 커널이 직접 한다.
 *
 * _start_bss, _end_bss: 링커 스크립트(kernel.lds)가 정의한
 * BSS 영역의 시작/끝 주소.
 * ============================================================ */
static void
bss_init (void) {
	extern char _start_bss, _end_bss;
	memset (&_start_bss, 0, &_end_bss - &_start_bss);
}

/* ============================================================
 * paging_init -- 커널 페이지 테이블 구축
 *
 * x86-64의 4단계 페이지 테이블(PML4 -> PDPT -> PD -> PT)을 만들어서
 * 물리 메모리 [0 ~ mem_end]를 커널 가상 주소에 1:1 매핑한다.
 *
 * 매핑 규칙:
 *   물리 주소 pa -> 가상 주소 LOADER_KERN_BASE + pa
 *
 * 커널 코드(.text) 영역은 쓰기 금지(PTE_W 제거)로 보호한다.
 * 이렇게 하면 코드 영역을 실수로 덮어쓰는 버그를 page fault로 잡을 수 있다.
 *
 * 마지막에 pml4_activate()로 CR3 레지스터를 갱신하면
 * CPU가 새 페이지 테이블을 사용하기 시작한다.
 * ============================================================ */
static void
paging_init (uint64_t mem_end) {
	uint64_t *pml4, *pte;
	int perm;

	/* 최상위 페이지 테이블(PML4)용 페이지를 할당하고 0으로 초기화한다. */
	pml4 = base_pml4 = palloc_get_page (PAL_ASSERT | PAL_ZERO);

	/* start: 커널 코드 시작 주소
	 * _end_kernel_text: 커널 코드 끝 주소 (이후는 데이터 영역) */
	extern char start, _end_kernel_text;

	/* 물리 메모리 전체를 페이지(4KB) 단위로 순회하며 매핑한다. */
	for (uint64_t pa = 0; pa < mem_end; pa += PGSIZE) {
		/* 물리 주소 -> 커널 가상 주소 변환 */
		uint64_t va = (uint64_t) ptov(pa);

		/* 기본 권한: Present + Writable */
		perm = PTE_P | PTE_W;

		/* 커널 코드(.text) 영역이면 쓰기 금지 */
		if ((uint64_t) &start <= va && va < (uint64_t) &_end_kernel_text)
			perm &= ~PTE_W;

		/* 4단계 페이지 테이블을 따라가며 PTE를 찾고(없으면 생성),
		 * 물리 주소 + 권한 비트를 기록한다. */
		if ((pte = pml4e_walk (pml4, va, 1)) != NULL)
			*pte = pa | perm;
	}

	/* CR3 레지스터에 새 PML4 주소를 로드하여 페이지 테이블을 활성화한다. */
	pml4_activate(0);
}

/* ============================================================
 * read_command_line -- 커널 명령줄을 argv 배열로 변환
 *
 * 부트로더(loader.S)가 커널 명령줄 문자열을 물리 메모리의
 * 고정 위치(LOADER_ARGS)에 저장해 둔다.
 * 인자 개수는 LOADER_ARG_CNT에 있다.
 *
 * 이 함수는 그 문자열들을 argv 배열에 담아 반환한다.
 *
 * 예시:
 *   pintos -- -q run alarm-single
 *   -> argc = 3
 *   -> argv = ["-q", "run", "alarm-single", NULL]
 *
 * "Kernel command line: -q run alarm-single" 출력이 이 함수에서 나온다.
 * ============================================================ */
static char **
read_command_line (void) {
	static char *argv[LOADER_ARGS_LEN / 2 + 1];
	char *p, *end;
	int argc;
	int i;

	/* 부트로더가 저장한 인자 개수를 물리 주소에서 읽어온다.
	 * ptov(): 물리 주소 -> 커널 가상 주소 변환 */
	argc = *(uint32_t *) ptov (LOADER_ARG_CNT);

	/* 인자 문자열이 저장된 시작 주소와 끝 주소 */
	p = ptov (LOADER_ARGS);
	end = p + LOADER_ARGS_LEN;

	/* NULL로 구분된 문자열들을 하나씩 argv에 담는다. */
	for (i = 0; i < argc; i++) {
		if (p >= end)
			PANIC ("command line arguments overflow");

		argv[i] = p;
		p += strnlen (p, end - p) + 1;  /* 다음 문자열로 이동 */
	}
	argv[argc] = NULL;  /* argv 배열의 끝을 표시 */

	/* 커널 명령줄을 화면에 출력한다.
	 * 공백이 포함된 인자는 따옴표로 감싼다. */
	printf ("Kernel command line:");
	for (i = 0; i < argc; i++)
		if (strchr (argv[i], ' ') == NULL)
			printf (" %s", argv[i]);
		else
			printf (" '%s'", argv[i]);
	printf ("\n");

	return argv;
}

/* ============================================================
 * parse_options -- 커널 명령줄에서 옵션을 파싱
 *
 * '-'로 시작하는 인자를 옵션으로 인식하여 처리한다.
 * 옵션이 아닌 첫 번째 인자(예: "run")를 가리키는 포인터를 반환한다.
 *
 * 지원하는 옵션:
 *   -h        도움말 출력 후 종료
 *   -q        테스트 완료 후 QEMU 자동 종료
 *   -f        파일 시스템 포맷 (FILESYS 모드)
 *   -rs=SEED  난수 시드 설정
 *   -mlfqs    MLFQS 스케줄러 활성화 (thread_mlfqs = true)
 *   -ul=COUNT 유저 메모리 페이지 제한 (USERPROG 모드)
 * ============================================================ */
static char **
parse_options (char **argv) {
	for (; *argv != NULL && **argv == '-'; argv++) {
		char *save_ptr;
		/* "=" 기준으로 이름과 값을 분리한다.
		 * 예: "-rs=42" -> name="-rs", value="42" */
		char *name = strtok_r (*argv, "=", &save_ptr);
		char *value = strtok_r (NULL, "", &save_ptr);

		if (!strcmp (name, "-h"))
			usage ();
		else if (!strcmp (name, "-q"))
			power_off_when_done = true;
#ifdef FILESYS
		else if (!strcmp (name, "-f"))
			format_filesys = true;
#endif
		else if (!strcmp (name, "-rs"))
			random_init (atoi (value));
		else if (!strcmp (name, "-mlfqs"))
			/* MLFQS 스케줄러를 켠다.
			 * 이 플래그가 true이면:
			 *   - priority donation이 비활성화된다.
			 *   - thread_set_priority()가 무시된다.
			 *   - timer_interrupt()에서 우선순위를 자동 계산한다. */
			thread_mlfqs = true;
#ifdef USERPROG
		else if (!strcmp (name, "-ul"))
			user_page_limit = atoi (value);
		else if (!strcmp (name, "-threads-tests"))
			thread_tests = true;
#endif
		else
			PANIC ("unknown option `%s' (use -h for help)", name);
	}

	return argv;
}

/* ============================================================
 * run_task -- 테스트 하나를 실행
 *
 * argv[1]에 있는 테스트 이름(예: "alarm-single")을 받아서
 * run_test()를 호출한다.
 *
 * USERPROG 모드에서는 유저 프로세스를 생성할 수도 있지만,
 * Project 1에서는 run_test()로 스레드 테스트를 직접 실행한다.
 *
 * run_test()는 tests/threads/tests.c에 정의되어 있고,
 * 테스트 이름으로 함수 포인터를 찾아서 호출한다.
 * ============================================================ */
static void
run_task (char **argv) {
	const char *task = argv[1];

	printf ("Executing '%s':\n", task);
#ifdef USERPROG
	if (thread_tests){
		run_test (task);
	} else {
		/* 유저 프로그램을 프로세스로 실행하고 종료를 기다린다. */
		process_wait (process_create_initd (task));
	}
#else
	/* Project 1: 스레드 테스트를 직접 실행한다. */
	run_test (task);
#endif
	printf ("Execution of '%s' complete.\n", task);
}

/* ============================================================
 * run_actions -- 커널 명령줄의 동작들을 순서대로 실행
 *
 * "run alarm-single" 같은 동작을 actions 테이블에서 찾아서
 * 대응하는 함수를 호출한다.
 *
 * actions 테이블:
 *   "run"  -> run_task() (인자 2개: "run" + 테스트이름)
 *   "ls"   -> fsutil_ls()  (FILESYS 모드)
 *   "cat"  -> fsutil_cat() (FILESYS 모드)
 *   ...
 *
 * 동작 이름이 테이블에 없으면 PANIC으로 커널이 죽는다.
 * ============================================================ */
static void
run_actions (char **argv) {
	/* 동작 구조체: 이름, 인자 수, 실행 함수 */
	struct action {
		char *name;                       /* 동작 이름 */
		int argc;                         /* 인자 수 (동작 이름 포함) */
		void (*function) (char **argv);   /* 실행할 함수 포인터 */
	};

	/* 지원하는 동작 테이블. NULL로 끝난다. */
	static const struct action actions[] = {
		{"run", 2, run_task},
#ifdef FILESYS
		{"ls", 1, fsutil_ls},
		{"cat", 2, fsutil_cat},
		{"rm", 2, fsutil_rm},
		{"put", 2, fsutil_put},
		{"get", 2, fsutil_get},
#endif
		{NULL, 0, NULL},
	};

	/* argv를 순회하며 동작을 하나씩 실행한다. */
	while (*argv != NULL) {
		const struct action *a;
		int i;

		/* 동작 이름을 테이블에서 찾는다. */
		for (a = actions; ; a++)
			if (a->name == NULL)
				PANIC ("unknown action `%s' (use -h for help)", *argv);
			else if (!strcmp (*argv, a->name))
				break;

		/* 필요한 인자가 충분한지 확인한다.
		 * 예: "run"은 뒤에 테스트 이름이 필요하다. */
		for (i = 1; i < a->argc; i++)
			if (argv[i] == NULL)
				PANIC ("action `%s' requires %d argument(s)", *argv, a->argc - 1);

		/* 동작 함수를 호출하고, 소비한 인자 수만큼 argv를 전진시킨다. */
		a->function (argv);
		argv += a->argc;
	}

}

/* ============================================================
 * usage -- 도움말을 출력하고 종료
 *
 * -h 옵션을 주면 이 함수가 호출된다.
 * 사용 가능한 옵션과 동작 목록을 출력한 뒤 power_off()로 종료한다.
 * ============================================================ */
static void
usage (void) {
	printf ("\nCommand line syntax: [OPTION...] [ACTION...]\n"
			"Options must precede actions.\n"
			"Actions are executed in the order specified.\n"
			"\nAvailable actions:\n"
#ifdef USERPROG
			"  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
			"  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
			"  ls                 List files in the root directory.\n"
			"  cat FILE           Print FILE to the console.\n"
			"  rm FILE            Delete FILE.\n"
			"Use these actions indirectly via `pintos' -g and -p options:\n"
			"  put FILE           Put FILE into file system from scratch disk.\n"
			"  get FILE           Get FILE from file system into scratch disk.\n"
#endif
			"\nOptions:\n"
			"  -h                 Print this help message and power off.\n"
			"  -q                 Power off VM after actions or on panic.\n"
			"  -f                 Format file system disk during startup.\n"
			"  -rs=SEED           Set random number seed to SEED.\n"
			"  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
			"  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
			);
	power_off ();
}


/* ============================================================
 * power_off -- QEMU를 종료
 *
 * QEMU의 ACPI 전원 관리 포트(0x604)에 종료 명령(0x2000)을 보낸다.
 * outw()는 x86 I/O 포트에 16비트 값을 쓰는 인라인 어셈블리 함수다.
 *
 * for(;;)는 혹시 종료 명령이 안 먹힐 경우를 대비한 무한 루프다.
 * 정상적으로는 outw() 직후 QEMU가 꺼진다.
 * ============================================================ */
void
power_off (void) {
#ifdef FILESYS
	/* 파일 시스템의 버퍼를 디스크에 쓰고 정리한다. */
	filesys_done ();
#endif

	/* 실행 통계(타이머 틱 수, 스레드 틱 수 등)를 출력한다. */
	print_stats ();

	printf ("Powering off...\n");
	outw (0x604, 0x2000);               /* QEMU ACPI 전원 종료 명령 */
	for (;;);                            /* 종료 안 되면 여기서 멈춤 */
}

/* ============================================================
 * print_stats -- 실행 통계 출력
 *
 * 각 서브시스템의 통계를 출력한다.
 * 테스트 실행 결과에서 보이는 "Timer: 580 ticks",
 * "Thread: 0 idle ticks, 581 kernel ticks" 등이 여기서 나온다.
 *
 * idle ticks가 0이면 CPU가 한 번도 쉬지 못했다는 뜻이다.
 * Phase 1(Alarm Clock) 구현 전에는 busy-wait 때문에 idle ticks = 0이다.
 * ============================================================ */
static void
print_stats (void) {
	timer_print_stats ();    /* "Timer: N ticks" */
	thread_print_stats ();   /* "Thread: N idle, N kernel, N user ticks" */
#ifdef FILESYS
	disk_print_stats ();     /* 디스크 읽기/쓰기 횟수 */
#endif
	console_print_stats ();  /* 콘솔 출력 문자 수 */
	kbd_print_stats ();      /* 키보드 입력 횟수 */
#ifdef USERPROG
	exception_print_stats (); /* 예외 발생 횟수 */
#endif
}
