/* ============================================================
 * interrupt.c -- 인터럽트 처리 시스템
 *
 * x86-64 인터럽트/예외를 관리하는 파일.
 * IDT(Interrupt Descriptor Table) 설정, PIC(8259A) 초기화,
 * 인터럽트 핸들러 등록 및 디스패치를 담당한다.
 *
 * 인터럽트의 두 종류:
 *
 * 1. 내부 인터럽트 (예외, exception)
 *    - CPU 내부에서 발생: divide by zero, page fault, system call 등
 *    - 벡터 번호 0x00 ~ 0x1F
 *    - 인터럽트를 켠 상태(INTR_ON)로 처리 가능 (트랩 게이트)
 *
 * 2. 외부 인터럽트 (하드웨어 인터럽트)
 *    - CPU 외부 디바이스에서 발생: 타이머, 키보드, 디스크 등
 *    - 벡터 번호 0x20 ~ 0x2F (PIC가 재매핑)
 *    - 인터럽트를 끈 상태(INTR_OFF)로 처리 (인터럽트 게이트)
 *    - 중첩 불가, 잠들기 금지
 *    - intr_yield_on_return()으로 선점 요청 가능
 *
 * Project 1에서 직접 수정할 일은 없지만, timer_interrupt()가
 * 외부 인터럽트로 등록되어 매 틱마다 호출되는 구조를 이해해야 한다.
 *
 * 인터럽트 처리 흐름:
 *   하드웨어 인터럽트 발생
 *   -> CPU가 IDT[vec_no]에서 핸들러 주소를 찾음
 *   -> intr_stubs[vec_no] (intr-stubs.S) 호출
 *   -> intr_handler() (이 파일) 호출
 *   -> 등록된 핸들러 함수 실행 (예: timer_interrupt)
 *   -> PIC에 EOI 전송
 *   -> yield_on_return이면 thread_yield()
 * ============================================================ */

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

/* x86-64 인터럽트 벡터의 총 개수 (0~255) */
#define INTR_CNT 256

/* ============================================================
 * IDT 게이트 디스크립터 구조체
 *
 * CPU가 인터럽트를 받으면 IDT에서 이 구조체를 읽어
 * 핸들러 함수의 주소와 권한을 확인한다.
 *
 * 게이트 종류:
 *   type=14: 인터럽트 게이트 -- 진입 시 인터럽트 자동 OFF
 *   type=15: 트랩 게이트 -- 진입 시 인터럽트 상태 유지
 *
 * DPL (Descriptor Privilege Level):
 *   0: 커널만 호출 가능 (하드웨어 인터럽트, 예외)
 *   3: 유저모드에서도 호출 가능 (시스템 콜)
 *
 * 핸들러 주소는 off_15_0, off_31_16, off_32_63로 분할 저장된다.
 * (x86 역사적 이유로 연속되지 않음)
 * ============================================================ */
struct gate {
	unsigned off_15_0 : 16;   /* 핸들러 주소의 비트 0~15 */
	unsigned ss : 16;         /* 세그먼트 셀렉터 (항상 커널 코드 세그먼트) */
	unsigned ist : 3;         /* IST 인덱스 (0이면 사용 안 함) */
	unsigned rsv1 : 5;        /* 예약 (0이어야 함) */
	unsigned type : 4;        /* 게이트 종류: 14=인터럽트, 15=트랩 */
	unsigned s : 1;           /* 시스템 비트 (0이어야 함) */
	unsigned dpl : 2;         /* 권한 레벨: 0=커널, 3=유저 */
	unsigned p : 1;           /* Present 비트: 1이면 유효 */
	unsigned off_31_16 : 16;  /* 핸들러 주소의 비트 16~31 */
	uint32_t off_32_63;       /* 핸들러 주소의 비트 32~63 */
	uint32_t rsv2;            /* 예약 */
};

/* IDT(Interrupt Descriptor Table).
 * 256개의 게이트 디스크립터 배열.
 * CPU가 인터럽트 N을 받으면 idt[N]에서 핸들러를 찾는다. */
static struct gate idt[INTR_CNT];

/* IDTR 레지스터에 로드할 IDT의 주소와 크기.
 * lidt() 명령으로 CPU에 알려준다. */
static struct desc_ptr idt_desc = {
	.size = sizeof(idt) - 1,
	.address = (uint64_t) idt
};

/* ============================================================
 * 게이트 디스크립터 생성 매크로
 *
 * make_gate: 핸들러 함수 주소, DPL, 게이트 타입으로 게이트를 만든다.
 * make_intr_gate: 인터럽트 게이트 (type=14, 진입 시 인터럽트 OFF)
 * make_trap_gate: 트랩 게이트 (type=15, 진입 시 인터럽트 유지)
 *
 * 핸들러 주소를 16/16/32비트로 분할하여 gate 구조체에 넣는다.
 * ============================================================ */
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

/* 인터럽트 게이트 생성 (외부 인터럽트용, 진입 시 인터럽트 OFF) */
#define make_intr_gate(g, function, dpl) make_gate((g), (function), (dpl), 14)

/* 트랩 게이트 생성 (내부 예외용, 진입 시 인터럽트 상태 유지) */
#define make_trap_gate(g, function, dpl) make_gate((g), (function), (dpl), 15)


/* 각 인터럽트 벡터에 등록된 핸들러 함수 포인터 배열.
 * NULL이면 해당 벡터에 핸들러가 없다. */
static intr_handler_func *intr_handlers[INTR_CNT];

/* 각 인터럽트 벡터의 이름 (디버깅용). */
static const char *intr_names[INTR_CNT];

/* 외부 인터럽트 처리 상태 플래그.
 *
 * in_external_intr: 현재 외부 인터럽트를 처리 중이면 true.
 *   외부 인터럽트는 중첩 불가하므로, true일 때 다른 외부 인터럽트가
 *   오면 ASSERT 실패한다.
 *
 * yield_on_return: true이면 인터럽트 처리가 끝난 후 thread_yield()를 호출.
 *   thread_tick()에서 타임 슬라이스가 만료되면 이 플래그를 켠다.
 *   인터럽트 핸들러 안에서는 thread_yield()를 직접 호출할 수 없으므로
 *   (인터럽트 OFF 상태이므로) 이 플래그로 지연 처리한다. */
static bool in_external_intr;
static bool yield_on_return;

/* PIC(Programmable Interrupt Controller) 관련 함수 전방 선언 */
static void pic_init (void);
static void pic_end_of_interrupt (int irq);

/* 모든 인터럽트의 공통 핸들러 (intr-stubs.S에서 호출) */
void intr_handler (struct intr_frame *args);

/* ============================================================
 * intr_get_level -- 현재 인터럽트 활성화 상태를 반환
 *
 * CPU의 EFLAGS 레지스터에서 IF(Interrupt Flag) 비트를 확인한다.
 *   IF = 1 -> INTR_ON (인터럽트 활성화)
 *   IF = 0 -> INTR_OFF (인터럽트 비활성화)
 *
 * pushfq로 EFLAGS를 스택에 푸시하고, popq로 변수에 읽어온다.
 * ============================================================ */
enum intr_level
intr_get_level (void) {
	uint64_t flags;

	asm volatile ("pushfq; popq %0" : "=g" (flags));

	return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}

/* ============================================================
 * intr_set_level -- 인터럽트 상태를 지정한 레벨로 설정
 *
 * INTR_ON이면 인터럽트를 켜고, INTR_OFF이면 끈다.
 * 이전 상태를 반환한다.
 *
 * 일반적인 사용 패턴:
 *   old_level = intr_disable();
 *   ... 임계 영역 ...
 *   intr_set_level(old_level);    // 이전 상태로 복원
 * ============================================================ */
enum intr_level
intr_set_level (enum intr_level level) {
	return level == INTR_ON ? intr_enable () : intr_disable ();
}

/* ============================================================
 * intr_enable -- 인터럽트 활성화
 *
 * x86 sti 명령어로 EFLAGS의 IF 비트를 1로 설정한다.
 * 이 시점부터 하드웨어 인터럽트(타이머 등)가 CPU에 전달된다.
 *
 * 인터럽트 핸들러 안에서는 호출 금지 (ASSERT).
 * 이전 인터럽트 상태를 반환한다.
 * ============================================================ */
enum intr_level
intr_enable (void) {
	enum intr_level old_level = intr_get_level ();
	ASSERT (!intr_context ());

	asm volatile ("sti");

	return old_level;
}

/* ============================================================
 * intr_disable -- 인터럽트 비활성화
 *
 * x86 cli 명령어로 EFLAGS의 IF 비트를 0으로 설정한다.
 * 이 시점부터 하드웨어 인터럽트가 보류된다 (다시 켤 때까지).
 *
 * 임계 영역(critical section)을 보호하는 가장 원시적인 방법.
 * 이전 인터럽트 상태를 반환한다.
 *
 * "memory" 클로버: 컴파일러가 cli 전후의 메모리 접근 순서를
 * 최적화로 바꾸지 못하게 한다.
 * ============================================================ */
enum intr_level
intr_disable (void) {
	enum intr_level old_level = intr_get_level ();

	asm volatile ("cli" : : : "memory");

	return old_level;
}

/* ============================================================
 * intr_init -- 인터럽트 시스템 초기화
 *
 * 1. PIC(8259A) 초기화: IRQ 0~15를 벡터 0x20~0x2F로 재매핑
 * 2. IDT 256개 엔트리를 모두 인터럽트 게이트로 초기화
 * 3. IDT를 CPU에 등록 (lidt 명령)
 * 4. 표준 예외 이름을 intr_names에 설정
 *
 * 이 함수 이후 intr_register_ext()나 intr_register_int()로
 * 개별 핸들러를 등록할 수 있다.
 * ============================================================ */
void
intr_init (void) {
	int i;

	/* 1. PIC 초기화: IRQ를 벡터 0x20~0x2F로 재매핑한다.
	 *    기본 설정은 IRQ 0~15 -> 벡터 0~15인데,
	 *    이 벡터들은 CPU 예외(divide by zero 등)와 겹치므로 재매핑 필수. */
	pic_init ();

	/* 2. IDT 256개를 모두 기본 인터럽트 게이트(DPL=0)로 초기화한다.
	 *    intr_stubs[i]는 intr-stubs.S에 정의된 어셈블리 스텁 함수.
	 *    각 스텁은 레지스터를 저장하고 intr_handler()를 호출한다. */
	for (i = 0; i < INTR_CNT; i++) {
		make_intr_gate(&idt[i], intr_stubs[i], 0);
		intr_names[i] = "unknown";
	}

#ifdef USERPROG
	/* TSS(Task State Segment)를 로드한다.
	 * 유저모드 -> 커널모드 전환 시 사용할 커널 스택 주소가 여기에 있다. */
	ltr (SEL_TSS);
#endif

	/* 3. IDT를 CPU의 IDTR 레지스터에 등록한다.
	 *    이후 인터럽트 발생 시 CPU가 이 IDT를 참조한다. */
	lidt(&idt_desc);

	/* 4. 표준 x86 예외 이름을 등록한다 (디버깅 메시지용).
	 *    0~19번이 CPU 예외이고, 15번은 Intel 예약이라 없다. */
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

/* ============================================================
 * register_handler -- 인터럽트 핸들러를 IDT에 등록 (내부 함수)
 *
 * vec_no: 인터럽트 벡터 번호
 * dpl: 권한 레벨 (0=커널, 3=유저)
 * level: 핸들러 실행 시 인터럽트 상태
 *   INTR_ON -> 트랩 게이트 (인터럽트 켜진 채로 실행)
 *   INTR_OFF -> 인터럽트 게이트 (인터럽트 끄고 실행)
 * handler: 핸들러 함수 포인터
 * name: 디버깅용 이름
 *
 * 같은 벡터에 중복 등록하면 ASSERT 실패.
 * ============================================================ */
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

/* ============================================================
 * intr_register_ext -- 외부 인터럽트 핸들러 등록
 *
 * 벡터 번호 0x20~0x2F (IRQ 0~15)에 대한 핸들러를 등록한다.
 * 외부 인터럽트는 항상 인터럽트 OFF 상태로 처리된다 (인터럽트 게이트).
 *
 * 예시:
 *   timer_init()에서 intr_register_ext(0x20, timer_interrupt, "8254 Timer")
 *   -> IRQ 0(타이머)이 벡터 0x20에 매핑되어 있으므로
 *      타이머 인터럽트 발생 시 timer_interrupt()가 호출된다.
 * ============================================================ */
void
intr_register_ext (uint8_t vec_no, intr_handler_func *handler,
		const char *name) {
	ASSERT (vec_no >= 0x20 && vec_no <= 0x2f);
	register_handler (vec_no, 0, INTR_OFF, handler, name);
}

/* ============================================================
 * intr_register_int -- 내부 인터럽트(예외) 핸들러 등록
 *
 * 벡터 번호 0x00~0x1F 또는 0x30 이상에 대한 핸들러를 등록한다.
 * (0x20~0x2F는 외부 인터럽트용이므로 제외)
 *
 * DPL에 따라 유저모드에서 호출 가능 여부가 결정된다:
 *   DPL=0: 커널만 (page fault, general protection 등)
 *   DPL=3: 유저도 가능 (시스템 콜)
 *
 * level에 따라 게이트 종류가 결정된다:
 *   INTR_ON: 트랩 게이트 (인터럽트 켠 채로 핸들러 실행)
 *   INTR_OFF: 인터럽트 게이트 (인터럽트 끄고 핸들러 실행)
 * ============================================================ */
void
intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name)
{
	ASSERT (vec_no < 0x20 || vec_no > 0x2f);
	register_handler (vec_no, dpl, level, handler, name);
}

/* ============================================================
 * intr_context -- 현재 외부 인터럽트 처리 중인지 확인
 *
 * true이면 현재 코드가 외부 인터럽트 핸들러 안에서 실행 중이다.
 * false이면 일반 스레드 컨텍스트에서 실행 중이다.
 *
 * 이 값이 true일 때 할 수 없는 것:
 *   - thread_block() (잠들기 금지)
 *   - sema_down() (대기 금지)
 *   - lock_acquire() (대기 금지)
 *   - intr_enable() (외부 인터럽트 중첩 금지)
 * ============================================================ */
bool
intr_context (void) {
	return in_external_intr;
}

/* ============================================================
 * intr_yield_on_return -- 인터럽트 복귀 시 thread_yield() 요청
 *
 * 외부 인터럽트 처리 중에만 호출할 수 있다.
 * 이 함수를 호출하면, 인터럽트 핸들러가 끝난 후
 * intr_handler()의 마지막에서 thread_yield()가 호출된다.
 *
 * 사용 시점:
 *   thread_tick()에서 thread_ticks >= TIME_SLICE이면 이 함수를 호출.
 *   -> 타임 슬라이스가 만료되었으니 선점하라는 요청.
 *
 * 왜 직접 thread_yield()를 안 부르나:
 *   외부 인터럽트 핸들러는 인터럽트가 꺼진 상태이고,
 *   thread_yield()는 인터럽트 컨텍스트에서 호출하면 ASSERT 실패.
 *   그래서 플래그만 켜고, 핸들러가 끝난 후에 yield한다.
 * ============================================================ */
void
intr_yield_on_return (void) {
	ASSERT (intr_context ());
	yield_on_return = true;
}

/* ============================================================
 * 8259A PIC (Programmable Interrupt Controller)
 *
 * PC에는 2개의 8259A PIC 칩이 있다:
 *   Master PIC: 포트 0x20, 0x21 (IRQ 0~7 관리)
 *   Slave PIC:  포트 0xA0, 0xA1 (IRQ 8~15 관리)
 *
 * Slave는 Master의 IRQ 2에 캐스케이드(연결)되어 있다.
 *
 * 기본 설정에서는 IRQ 0~15이 벡터 0~15에 매핑되는데,
 * 이 벡터들은 CPU 예외와 겹친다.
 * 그래서 pic_init()에서 IRQ 0~15을 벡터 0x20~0x2F로 재매핑한다.
 *
 * 벡터 매핑:
 *   IRQ 0 (타이머)     -> 벡터 0x20
 *   IRQ 1 (키보드)     -> 벡터 0x21
 *   IRQ 2 (Slave 연결) -> 벡터 0x22
 *   ...
 *   IRQ 14 (디스크)    -> 벡터 0x2E
 *   IRQ 15             -> 벡터 0x2F
 * ============================================================ */

/* ============================================================
 * pic_init -- PIC 초기화
 *
 * ICW(Initialization Command Word) 시퀀스로 Master/Slave PIC를
 * 초기화하고, IRQ를 벡터 0x20~0x2F로 재매핑한다.
 *
 * 초기화 순서:
 *   1. 모든 IRQ를 마스크(비활성화)
 *   2. ICW1~ICW4로 Master PIC 설정
 *   3. ICW1~ICW4로 Slave PIC 설정
 *   4. 모든 IRQ 마스크 해제(활성화)
 * ============================================================ */
static void
pic_init (void) {
	/* 모든 인터럽트를 임시로 차단한다 (초기화 중 오작동 방지) */
	outb (0x21, 0xff);
	outb (0xa1, 0xff);

	/* Master PIC 초기화 */
	outb (0x20, 0x11); /* ICW1: 단일 모드, 엣지 트리거, ICW4 필요 */
	outb (0x21, 0x20); /* ICW2: IRQ 0~7 -> 벡터 0x20~0x27로 재매핑 */
	outb (0x21, 0x04); /* ICW3: IRQ 2에 Slave PIC 연결 */
	outb (0x21, 0x01); /* ICW4: 8086 모드, 일반 EOI, 비버퍼링 */

	/* Slave PIC 초기화 */
	outb (0xa0, 0x11); /* ICW1: 단일 모드, 엣지 트리거, ICW4 필요 */
	outb (0xa1, 0x28); /* ICW2: IRQ 8~15 -> 벡터 0x28~0x2F로 재매핑 */
	outb (0xa1, 0x02); /* ICW3: Slave ID = 2 (Master의 IRQ 2에 연결) */
	outb (0xa1, 0x01); /* ICW4: 8086 모드, 일반 EOI, 비버퍼링 */

	/* 모든 인터럽트 마스크를 해제한다 (모든 IRQ 활성화) */
	outb (0x21, 0x00);
	outb (0xa1, 0x00);
}

/* ============================================================
 * pic_end_of_interrupt -- PIC에 인터럽트 처리 완료(EOI) 전송
 *
 * 외부 인터럽트 처리가 끝나면 반드시 PIC에 EOI를 보내야 한다.
 * EOI를 보내지 않으면 PIC가 해당 IRQ를 다시 전달하지 않는다.
 *
 * Master PIC에는 항상 EOI를 보내고,
 * Slave PIC의 IRQ(0x28~0x2F)이면 Slave에도 추가로 보낸다.
 * ============================================================ */
static void
pic_end_of_interrupt (int irq) {
	ASSERT (irq >= 0x20 && irq < 0x30);

	/* Master PIC에 EOI 전송 (항상) */
	outb (0x20, 0x20);

	/* Slave PIC 인터럽트(IRQ 8~15 = 벡터 0x28~0x2F)이면
	 * Slave에도 EOI 전송 */
	if (irq >= 0x28)
		outb (0xa0, 0x20);
}

/* ============================================================
 * intr_handler -- 모든 인터럽트의 공통 디스패처
 *
 * intr-stubs.S의 어셈블리 스텁이 레지스터를 intr_frame에 저장한 후
 * 이 함수를 호출한다.
 *
 * frame->vec_no로 어떤 인터럽트인지 판별하고,
 * 등록된 핸들러 함수를 호출한다.
 *
 * 외부 인터럽트(0x20~0x2F) 처리 시 추가 작업:
 *   1. in_external_intr = true 설정 (intr_context() 반환값)
 *   2. yield_on_return = false 초기화
 *   3. 핸들러 실행
 *   4. PIC에 EOI 전송
 *   5. yield_on_return이 true이면 thread_yield() 호출
 *
 * 0x27, 0x2F에 핸들러가 없으면 무시한다:
 *   이 벡터들은 하드웨어 결함이나 경쟁 조건으로 인해
 *   스퓨리어스(spurious) 인터럽트가 발생할 수 있다.
 *
 * 등록되지 않은 다른 벡터가 발생하면 레지스터를 덤프하고
 * PANIC으로 커널을 멈춘다.
 * ============================================================ */
void
intr_handler (struct intr_frame *frame) {
	bool external;
	intr_handler_func *handler;

	/* 외부 인터럽트 여부 판별 (벡터 0x20~0x2F) */
	external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;

	if (external) {
		/* 외부 인터럽트 전처리:
		 * - 인터럽트가 꺼져있어야 한다 (중첩 방지)
		 * - 이미 외부 인터럽트 처리 중이면 안 된다 */
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (!intr_context ());

		in_external_intr = true;    /* 외부 인터럽트 처리 중 표시 */
		yield_on_return = false;    /* yield 요청 초기화 */
	}

	/* 등록된 핸들러 호출 */
	handler = intr_handlers[frame->vec_no];
	if (handler != NULL)
		handler (frame);
	else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {
		/* 핸들러 없음 + 스퓨리어스 인터럽트 가능 벡터 -> 무시 */
	} else {
		/* 핸들러 없고 스퓨리어스도 아님 -> 예상치 못한 인터럽트 */
		intr_dump_frame (frame);
		PANIC ("Unexpected interrupt");
	}

	/* 외부 인터럽트 후처리 */
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (intr_context ());

		in_external_intr = false;           /* 외부 인터럽트 처리 완료 */
		pic_end_of_interrupt (frame->vec_no); /* PIC에 EOI 전송 */

		/* thread_tick()이 선점을 요청했으면 여기서 yield */
		if (yield_on_return)
			thread_yield ();
	}
}

/* ============================================================
 * intr_dump_frame -- 인터럽트 프레임의 레지스터를 콘솔에 출력
 *
 * 디버깅용. 예상치 못한 인터럽트나 PANIC 시 호출된다.
 * 모든 범용 레지스터, 세그먼트 레지스터, rip, rflags 등을 출력.
 *
 * CR2: 마지막 page fault의 가상 주소.
 *      page fault(#PF, vec 14) 디버깅에 유용하다.
 * ============================================================ */
void
intr_dump_frame (const struct intr_frame *f) {
	uint64_t cr2 = rcr2();   /* 마지막 page fault 주소 */
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

/* 인터럽트 벡터 VEC의 이름을 반환한다. */
const char *
intr_name (uint8_t vec) {
	return intr_names[vec];
}
