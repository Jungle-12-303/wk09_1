#ifndef THREADS_FLAGS_H
#define THREADS_FLAGS_H

/* x86-64 EFLAGS/RFLAGS 레지스터 비트 마스크.
 *
 * EFLAGS는 CPU 상태를 제어하는 레지스터다.
 * 인터럽트 활성화, 디버그 트랩 등의 플래그가 포함된다. */

#define FLAG_MBS   (1<<1)       /* Must Be Set: 항상 1이어야 하는 예약 비트. */
#define FLAG_TF    (1<<8)       /* Trap Flag: 1이면 한 명령어씩 실행 (싱글 스텝 디버깅). */
#define FLAG_IF    (1<<9)       /* Interrupt Flag: 1이면 인터럽트 수신 (sti), 0이면 차단 (cli). */
#define FLAG_DF    (1<<10)      /* Direction Flag: 문자열 명령어 방향 (0=증가, 1=감소). */
#define FLAG_IOPL  (3<<12)      /* I/O Privilege Level: I/O 포트 접근에 필요한 특권 레벨 (0~3). */
#define FLAG_AC    (1<<18)      /* Alignment Check: 정렬되지 않은 메모리 접근 시 예외 발생. */
#define FLAG_NT    (1<<14)      /* Nested Task: TSS를 통한 태스크 중첩 시 설정. */

#endif /* threads/flags.h */
