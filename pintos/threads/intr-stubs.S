/*=======================================================================
 * intr-stubs.S — 인터럽트 진입 스텁 및 핸들러 디스패처
 *=======================================================================
 *
 * ▶ 역할
 *   x86-64에서 인터럽트/예외가 발생하면 CPU는 IDT(Interrupt Descriptor
 *   Table)를 참조하여 해당 벡터 번호의 핸들러로 점프한다.
 *   이 파일은 256개(0x00~0xFF) 벡터 각각에 대한 진입 스텁(stub)과
 *   공통 진입점(intr_entry)을 정의한다.
 *
 * ▶ 인터럽트 처리 흐름 (전체 그림)
 *
 *   [하드웨어/소프트웨어 인터럽트 발생]
 *          │
 *          ▼
 *   CPU가 자동으로 스택에 푸시:
 *     SS, RSP, RFLAGS, CS, RIP  (+ error_code: 일부 예외만)
 *          │
 *          ▼
 *   intrNN_stub  (이 파일에서 정의)
 *     - error_code 없는 경우: pushq $0 (가짜 에러 코드)
 *     - pushq $벡터번호
 *     - jmp intr_entry
 *          │
 *          ▼
 *   intr_entry  (이 파일에서 정의)
 *     - 모든 범용 레지스터 저장 (struct intr_frame 완성)
 *     - 세그먼트 레지스터를 커널 데이터 세그먼트로 설정
 *     - intr_handler(struct intr_frame *) 호출  (interrupt.c)
 *     - 레지스터 복원
 *     - iretq → 인터럽트 이전 상태로 복귀
 *
 * ▶ struct intr_frame 스택 레이아웃 (아래 → 위 = 낮은 주소 → 높은 주소)
 *
 *   ┌─────────────────┐  ← RSP (intr_handler 호출 시)
 *   │ R15              │  오프셋 0
 *   │ R14              │  오프셋 8
 *   │ R13              │  오프셋 16
 *   │ R12              │  오프셋 24
 *   │ R11              │  오프셋 32
 *   │ R10              │  오프셋 40
 *   │ R9               │  오프셋 48
 *   │ R8               │  오프셋 56
 *   │ RSI              │  오프셋 64
 *   │ RDI              │  오프셋 72
 *   │ RBP              │  오프셋 80
 *   │ RDX              │  오프셋 88
 *   │ RCX              │  오프셋 96
 *   │ RBX              │  오프셋 104
 *   │ RAX              │  오프셋 112
 *   │ ES (16비트)      │  오프셋 120 (하위 2바이트)
 *   │ DS (16비트)      │  오프셋 128 (하위 2바이트)
 *   │ vec_no (벡터번호) │  ← intrNN_stub이 푸시
 *   │ error_code       │  ← CPU 또는 stub이 푸시
 *   │ RIP              │  ← CPU가 자동 푸시
 *   │ CS               │
 *   │ RFLAGS           │
 *   │ RSP (이전)       │
 *   │ SS               │
 *   └─────────────────┘
 *
 * ▶ Project 1 관련
 *   직접 수정할 일은 없다. 하지만 타이머 인터럽트(벡터 0x20)가
 *   intr20_stub → intr_entry → intr_handler → timer_interrupt 순서로
 *   호출되는 과정을 이해해야 Alarm Clock과 스케줄링을 구현할 수 있다.
 *   특히 thread_yield()가 인터럽트 컨텍스트에서 직접 호출되지 않고
 *   yield_on_return 플래그를 통해 간접적으로 작동하는 이유가
 *   바로 이 인터럽트 프레임 구조 때문이다.
 *=======================================================================*/

#include "threads/loader.h"

/*=======================================================================
 * intr_entry — 모든 인터럽트의 공통 진입점
 *=======================================================================
 * intrNN_stub에서 jmp으로 여기에 도달한다.
 * 이 시점에서 스택에는 이미 다음이 쌓여 있다:
 *   [CPU 자동] SS, RSP, RFLAGS, CS, RIP
 *   [stub]     error_code(또는 0), vec_no
 *
 * 이 함수가 하는 일:
 *   1) 나머지 레지스터(RAX~R15, DS, ES)를 스택에 저장
 *      → struct intr_frame 구조체가 스택 위에 완성됨
 *   2) 세그먼트 레지스터를 커널 데이터 세그먼트(SEL_KDSEG)로 설정
 *      → 유저 모드에서 인터럽트가 왔을 경우를 대비
 *   3) RSP를 RDI에 넣어 intr_handler(frame) 호출
 *      → x86-64 calling convention: 첫 번째 인자 = RDI
 *   4) intr_handler() 리턴 후 모든 레지스터 복원
 *   5) iretq로 인터럽트 이전 상태로 복귀
 *=======================================================================*/
.section .text
.func intr_entry
intr_entry:
	/*-- 세그먼트 레지스터 저장을 위한 공간 확보 (DS + ES = 16바이트) --*/
	subq $16,%rsp
	movw %ds,8(%rsp)          /* DS 저장 */
	movw %es,0(%rsp)          /* ES 저장 */

	/*-- 범용 레지스터 15개 저장 (120바이트 = 15 × 8) --*/
	subq $120,%rsp
	movq %rax,112(%rsp)
	movq %rbx,104(%rsp)
	movq %rcx,96(%rsp)
	movq %rdx,88(%rsp)
	movq %rbp,80(%rsp)
	movq %rdi,72(%rsp)
	movq %rsi,64(%rsp)
	movq %r8,56(%rsp)
	movq %r9,48(%rsp)
	movq %r10,40(%rsp)
	movq %r11,32(%rsp)
	movq %r12,24(%rsp)
	movq %r13,16(%rsp)
	movq %r14,8(%rsp)
	movq %r15,0(%rsp)

	/*-- 문자열 명령어 방향 플래그 초기화 (위로 진행) --*/
	cld

	/*-- 세그먼트 레지스터를 커널 데이터 세그먼트로 설정 --*/
	/*   유저 모드에서 인터럽트가 왔을 경우, DS/ES/SS/FS/GS가
	 *   유저 세그먼트를 가리키고 있을 수 있으므로 커널용으로 교체 */
	movq $SEL_KDSEG, %rax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw %ax, %fs
	movw %ax, %gs

	/*-- intr_handler(struct intr_frame *frame) 호출 --*/
	movq %rsp,%rdi             /* RDI = RSP = &intr_frame (첫 번째 인자) */
	call intr_handler          /* interrupt.c의 intr_handler() 호출 */

	/*-- 레지스터 복원 (저장의 역순) --*/
	movq 0(%rsp), %r15
	movq 8(%rsp), %r14
	movq 16(%rsp), %r13
	movq 24(%rsp), %r12
	movq 32(%rsp), %r11
	movq 40(%rsp), %r10
	movq 48(%rsp), %r9
	movq 56(%rsp), %r8
	movq 64(%rsp), %rsi
	movq 72(%rsp), %rdi
	movq 80(%rsp), %rbp
	movq 88(%rsp), %rdx
	movq 96(%rsp), %rcx
	movq 104(%rsp), %rbx
	movq 112(%rsp), %rax
	addq $120, %rsp            /* 범용 레지스터 공간 해제 */

	/*-- DS, ES 복원 --*/
	movw 8(%rsp), %ds
	movw (%rsp), %es
	addq $32, %rsp             /* DS/ES(16) + vec_no(8) + error_code(8) 해제 */

	/*-- 인터럽트 복귀 --*/
	/*   iretq는 스택에서 RIP, CS, RFLAGS, RSP, SS를 팝하여
	 *   인터럽트 발생 직전 상태로 CPU를 복원한다. */
	iretq
.endfunc

/*=======================================================================
 * 인터럽트 스텁 (Interrupt Stubs) — 256개 벡터별 진입점
 *=======================================================================
 *
 * ▶ 왜 벡터마다 별도 스텁이 필요한가?
 *   CPU가 인터럽트 발생 시 벡터 번호를 스택에 자동으로 넣어주지 않는다.
 *   따라서 각 벡터별로 "내 번호는 N이다"를 push하는 코드가 필요하다.
 *
 * ▶ error_code 처리
 *   x86 예외 중 일부(#DF, #TS, #NP, #SS, #GP, #PF, #AC 등)는
 *   CPU가 자동으로 에러 코드를 스택에 푸시한다.
 *   나머지 예외 및 모든 외부 인터럽트는 에러 코드가 없다.
 *
 *   에러 코드가 있는 벡터(REAL):
 *     08(#DF), 0A(#TS), 0B(#NP), 0C(#SS), 0D(#GP), 0E(#PF),
 *     11(#AC), 18, 1A, 1B, 1D, 1E
 *
 *   에러 코드가 없는 벡터(zero):
 *     나머지 전부 → pushq $0 으로 가짜 에러 코드를 넣어
 *     struct intr_frame 레이아웃을 통일한다.
 *
 * ▶ STUB 매크로 동작
 *   STUB(NUMBER, TYPE) 매크로는 다음을 생성한다:
 *
 *   intrNN_stub:
 *       TYPE           ← zero: pushq $0  /  REAL: (아무것도 안 함)
 *       push $0xNN     ← 벡터 번호 푸시
 *       jmp intr_entry ← 공통 진입점으로 점프
 *
 *   그리고 .data 섹션의 intr_stubs[] 배열에 이 스텁의 주소를 기록한다.
 *   interrupt.c의 intr_init()이 이 배열을 읽어 IDT를 초기화한다.
 *
 * ▶ intr_stubs 배열
 *   .data 섹션에 256개 함수 포인터가 순서대로 나열된다:
 *     intr_stubs[0]   = &intr00_stub
 *     intr_stubs[1]   = &intr01_stub
 *     ...
 *     intr_stubs[255] = &intrff_stub
 *   interrupt.c에서 extern으로 참조하여 IDT 엔트리의 핸들러 주소로 사용.
 *=======================================================================*/

/*-- zero 매크로: 에러 코드가 없는 벡터용 → 가짜 에러 코드 0을 푸시 --*/
#define zero pushq $0;

/*-- REAL 매크로: CPU가 이미 에러 코드를 푸시한 벡터용 → 아무것도 안 함 --*/
#define REAL

/*-- intr_stubs 배열 시작 (.data 섹션) --*/
.section .data
.globl intr_stubs
intr_stubs:

/*-----------------------------------------------------------------------
 * STUB(NUMBER, TYPE) 매크로 정의
 *   .text 섹션에 intrNN_stub 함수를 생성하고,
 *   .data 섹션의 intr_stubs[]에 해당 함수 포인터를 추가한다.
 *-----------------------------------------------------------------------*/
#define STUB(NUMBER, TYPE)                      \
.section .text;                                  \
.globl intr##NUMBER##_stub;                     \
.func intr##NUMBER##_stub;			\
intr##NUMBER##_stub:                            \
	TYPE;                                   \
	push $0x##NUMBER;                       \
	jmp intr_entry;                         \
.endfunc; \
.section .data; \
.quad intr##NUMBER##_stub;

/*=======================================================================
 * 256개 스텁 생성
 *
 * 벡터 0x00~0x1F: CPU 예외 (Exception)
 *   0x00 #DE  Division Error              (zero - 에러코드 없음)
 *   0x01 #DB  Debug                       (zero)
 *   0x02 NMI  Non-Maskable Interrupt      (zero)
 *   0x03 #BP  Breakpoint                  (zero)
 *   0x04 #OF  Overflow                    (zero)
 *   0x05 #BR  Bound Range Exceeded        (zero)
 *   0x06 #UD  Invalid Opcode             (zero)
 *   0x07 #NM  Device Not Available        (zero)
 *   0x08 #DF  Double Fault               (REAL - 에러코드 있음)
 *   0x09      Coprocessor Segment Overrun (zero, 레거시)
 *   0x0A #TS  Invalid TSS                (REAL)
 *   0x0B #NP  Segment Not Present        (REAL)
 *   0x0C #SS  Stack-Segment Fault        (zero)
 *   0x0D #GP  General Protection         (REAL)
 *   0x0E #PF  Page Fault                 (REAL)
 *   0x0F      (예약)                     (zero)
 *   0x10 #MF  x87 FPU Error              (zero)
 *   0x11 #AC  Alignment Check            (REAL)
 *   0x12 #MC  Machine Check              (zero)
 *   0x13 #XM  SIMD Exception             (zero)
 *   0x14~0x1F (예약)                     (zero/REAL)
 *
 * 벡터 0x20~0x2F: 외부 하드웨어 인터럽트 (PIC 8259A가 리매핑)
 *   0x20 IRQ 0  = 타이머 (PIT)    ← Project 1에서 핵심!
 *   0x21 IRQ 1  = 키보드
 *   ...
 *   0x2E IRQ 14 = IDE 디스크
 *   0x2F IRQ 15 = IDE 디스크 (Secondary)
 *
 * 벡터 0x30~0xFF: 소프트웨어 인터럽트 및 사용자 정의
 *   자유롭게 사용 가능. Pintos에서는 0x30을 시스템콜(Project 2)에 사용.
 *=======================================================================*/

/* 벡터 0x00~0x0F: CPU 예외 */
STUB(00, zero) STUB(01, zero) STUB(02, zero) STUB(03, zero)
STUB(04, zero) STUB(05, zero) STUB(06, zero) STUB(07, zero)
STUB(08, REAL) STUB(09, zero) STUB(0a, REAL) STUB(0b, REAL)
STUB(0c, zero) STUB(0d, REAL) STUB(0e, REAL) STUB(0f, zero)

/* 벡터 0x10~0x1F: CPU 예외 (계속) */
STUB(10, zero) STUB(11, REAL) STUB(12, zero) STUB(13, zero)
STUB(14, zero) STUB(15, zero) STUB(16, zero) STUB(17, zero)
STUB(18, REAL) STUB(19, zero) STUB(1a, REAL) STUB(1b, REAL)
STUB(1c, zero) STUB(1d, REAL) STUB(1e, REAL) STUB(1f, zero)

/* 벡터 0x20~0x2F: 외부 하드웨어 인터럽트 (IRQ 0~15) */
/* 0x20 = 타이머 인터럽트 → timer_interrupt() → thread_tick() 호출 */
STUB(20, zero) STUB(21, zero) STUB(22, zero) STUB(23, zero)
STUB(24, zero) STUB(25, zero) STUB(26, zero) STUB(27, zero)
STUB(28, zero) STUB(29, zero) STUB(2a, zero) STUB(2b, zero)
STUB(2c, zero) STUB(2d, zero) STUB(2e, zero) STUB(2f, zero)

/* 벡터 0x30~0xFF: 소프트웨어 인터럽트 및 사용자 정의 */
STUB(30, zero) STUB(31, zero) STUB(32, zero) STUB(33, zero)
STUB(34, zero) STUB(35, zero) STUB(36, zero) STUB(37, zero)
STUB(38, zero) STUB(39, zero) STUB(3a, zero) STUB(3b, zero)
STUB(3c, zero) STUB(3d, zero) STUB(3e, zero) STUB(3f, zero)

STUB(40, zero) STUB(41, zero) STUB(42, zero) STUB(43, zero)
STUB(44, zero) STUB(45, zero) STUB(46, zero) STUB(47, zero)
STUB(48, zero) STUB(49, zero) STUB(4a, zero) STUB(4b, zero)
STUB(4c, zero) STUB(4d, zero) STUB(4e, zero) STUB(4f, zero)

STUB(50, zero) STUB(51, zero) STUB(52, zero) STUB(53, zero)
STUB(54, zero) STUB(55, zero) STUB(56, zero) STUB(57, zero)
STUB(58, zero) STUB(59, zero) STUB(5a, zero) STUB(5b, zero)
STUB(5c, zero) STUB(5d, zero) STUB(5e, zero) STUB(5f, zero)

STUB(60, zero) STUB(61, zero) STUB(62, zero) STUB(63, zero)
STUB(64, zero) STUB(65, zero) STUB(66, zero) STUB(67, zero)
STUB(68, zero) STUB(69, zero) STUB(6a, zero) STUB(6b, zero)
STUB(6c, zero) STUB(6d, zero) STUB(6e, zero) STUB(6f, zero)

STUB(70, zero) STUB(71, zero) STUB(72, zero) STUB(73, zero)
STUB(74, zero) STUB(75, zero) STUB(76, zero) STUB(77, zero)
STUB(78, zero) STUB(79, zero) STUB(7a, zero) STUB(7b, zero)
STUB(7c, zero) STUB(7d, zero) STUB(7e, zero) STUB(7f, zero)

STUB(80, zero) STUB(81, zero) STUB(82, zero) STUB(83, zero)
STUB(84, zero) STUB(85, zero) STUB(86, zero) STUB(87, zero)
STUB(88, zero) STUB(89, zero) STUB(8a, zero) STUB(8b, zero)
STUB(8c, zero) STUB(8d, zero) STUB(8e, zero) STUB(8f, zero)

STUB(90, zero) STUB(91, zero) STUB(92, zero) STUB(93, zero)
STUB(94, zero) STUB(95, zero) STUB(96, zero) STUB(97, zero)
STUB(98, zero) STUB(99, zero) STUB(9a, zero) STUB(9b, zero)
STUB(9c, zero) STUB(9d, zero) STUB(9e, zero) STUB(9f, zero)

STUB(a0, zero) STUB(a1, zero) STUB(a2, zero) STUB(a3, zero)
STUB(a4, zero) STUB(a5, zero) STUB(a6, zero) STUB(a7, zero)
STUB(a8, zero) STUB(a9, zero) STUB(aa, zero) STUB(ab, zero)
STUB(ac, zero) STUB(ad, zero) STUB(ae, zero) STUB(af, zero)

STUB(b0, zero) STUB(b1, zero) STUB(b2, zero) STUB(b3, zero)
STUB(b4, zero) STUB(b5, zero) STUB(b6, zero) STUB(b7, zero)
STUB(b8, zero) STUB(b9, zero) STUB(ba, zero) STUB(bb, zero)
STUB(bc, zero) STUB(bd, zero) STUB(be, zero) STUB(bf, zero)

STUB(c0, zero) STUB(c1, zero) STUB(c2, zero) STUB(c3, zero)
STUB(c4, zero) STUB(c5, zero) STUB(c6, zero) STUB(c7, zero)
STUB(c8, zero) STUB(c9, zero) STUB(ca, zero) STUB(cb, zero)
STUB(cc, zero) STUB(cd, zero) STUB(ce, zero) STUB(cf, zero)

STUB(d0, zero) STUB(d1, zero) STUB(d2, zero) STUB(d3, zero)
STUB(d4, zero) STUB(d5, zero) STUB(d6, zero) STUB(d7, zero)
STUB(d8, zero) STUB(d9, zero) STUB(da, zero) STUB(db, zero)
STUB(dc, zero) STUB(dd, zero) STUB(de, zero) STUB(df, zero)

STUB(e0, zero) STUB(e1, zero) STUB(e2, zero) STUB(e3, zero)
STUB(e4, zero) STUB(e5, zero) STUB(e6, zero) STUB(e7, zero)
STUB(e8, zero) STUB(e9, zero) STUB(ea, zero) STUB(eb, zero)
STUB(ec, zero) STUB(ed, zero) STUB(ee, zero) STUB(ef, zero)

STUB(f0, zero) STUB(f1, zero) STUB(f2, zero) STUB(f3, zero)
STUB(f4, zero) STUB(f5, zero) STUB(f6, zero) STUB(f7, zero)
STUB(f8, zero) STUB(f9, zero) STUB(fa, zero) STUB(fb, zero)
STUB(fc, zero) STUB(fd, zero) STUB(fe, zero) STUB(ff, zero)
