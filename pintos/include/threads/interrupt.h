#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* 인터럽트 활성화 상태. */
enum intr_level {
	INTR_OFF,             /* 인터럽트 비활성화 (cli 상태). */
	INTR_ON               /* 인터럽트 활성화 (sti 상태). */
};

enum intr_level intr_get_level (void);    /* 현재 인터럽트 상태 반환. */
enum intr_level intr_set_level (enum intr_level);  /* 인터럽트 상태 설정, 이전 상태 반환. */
enum intr_level intr_enable (void);       /* 인터럽트 활성화, 이전 상태 반환. */
enum intr_level intr_disable (void);      /* 인터럽트 비활성화, 이전 상태 반환. */

/* 범용 레지스터 저장 구조체.
 * 인터럽트/예외 발생 시 intr_entry(intr-stubs.S)가
 * 현재 실행 중이던 스레드의 레지스터를 이 순서로 스택에 푸시한다.
 * packed 속성으로 패딩 없이 배치. */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

/* 인터럽트 스택 프레임.
 * 인터럽트/예외 발생 시 CPU와 intr-stubs.S가 함께 구성하는 프레임.
 * 중단된 작업의 전체 상태가 여기에 저장되어 있어서,
 * 이 프레임을 복원하면(do_iret) 원래 작업으로 돌아갈 수 있다.
 * 컨텍스트 스위칭에서도 이 구조체를 사용한다. */
struct intr_frame {
	/* intr-stubs.S의 intr_entry가 푸시.
	 * 중단된 작업의 저장된 범용 레지스터. */
	struct gp_registers R;
	uint16_t es;                /* 추가 세그먼트 레지스터. */
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;                /* 데이터 세그먼트 레지스터. */
	uint16_t __pad3;
	uint32_t __pad4;
	/* intr-stubs.S의 intrNN_stub이 푸시. */
	uint64_t vec_no;            /* 인터럽트 벡터 번호 (0~255). */
	/* CPU가 자동 푸시하거나, 일관성을 위해 stub이 0으로 푸시.
	 * CPU는 원래 eip 바로 아래에 넣지만, 여기로 이동시켰다. */
	uint64_t error_code;        /* 에러 코드 (없으면 0). */
	/* CPU가 자동 푸시.
	 * 중단된 작업의 실행 상태. */
	uintptr_t rip;              /* 중단 지점의 명령어 주소. */
	uint16_t cs;                /* 코드 세그먼트. */
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags;            /* EFLAGS 레지스터 (인터럽트 플래그 등). */
	uintptr_t rsp;              /* 중단 시점의 스택 포인터. */
	uint16_t ss;                /* 스택 세그먼트. */
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);           /* IDT 초기화. */
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
	/* 외부(하드웨어) 인터럽트 핸들러 등록. 예: 타이머, 키보드. */
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
	/* 내부(소프트웨어) 인터럽트/예외 핸들러 등록. 예: 페이지 폴트. */
bool intr_context (void);        /* 현재 인터럽트 핸들러 내부인지 확인. */
void intr_yield_on_return (void); /* 인터럽트 복귀 시 yield 예약. */

void intr_dump_frame (const struct intr_frame *);  /* 프레임 내용 덤프 (디버깅용). */
const char *intr_name (uint8_t vec);               /* 벡터 번호의 이름 반환. */

#endif /* threads/interrupt.h */
