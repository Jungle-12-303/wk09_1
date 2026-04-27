#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* @lock 시스템 콜.
 *
 * 예전에는 시스템 콜 서비스가 인터럽트 핸들러로 처리되었다.
 * 예를 들어 Linux의 int 0x80이 그렇다. 하지만 x86-64에서는 제조사가
 * 시스템 콜 요청을 위한 효율적인 경로인 `syscall' 명령을 제공한다.
 *
 * syscall 명령은 Model Specific Register(MSR)에서 값을 읽어 동작한다.
 * 자세한 내용은 매뉴얼을 참고한다. */

#define MSR_STAR 0xc0000081         /* @lock 세그먼트 셀렉터 MSR. */
#define MSR_LSTAR 0xc0000082        /* @lock 롱 모드 SYSCALL 대상. */
#define MSR_SYSCALL_MASK 0xc0000084 /* @lock eflags용 마스크. */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* @lock syscall_entry가 사용자 영역 스택을 커널 모드 스택으로 교체하기 전까지
	 * 인터럽트 서비스 루틴은 어떤 인터럽트도 처리하면 안 된다.
	 * 따라서 FLAG_FL을 마스크했다. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* @lock 주요 시스템 콜 인터페이스. */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// @lock TODO: 여기에 구현을 작성한다.
	printf ("system call!\n");
	thread_exit ();
}
