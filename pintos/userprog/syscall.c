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

/* @lock
 * 시스템 콜.
 *
 * 이전에는 시스템 콜 서비스가 인터럽트 핸들러
 * (예: Linux의 int 0x80)로 처리되었다. 하지만 x86-64에서는
 * 제조사가 `syscall` 명령어라는 더 효율적인 시스템 콜 요청 경로를 제공한다.
 *
 * syscall 명령어는 Model Specific Register(MSR)에 저장된 값을 읽어 동작한다.
 * 자세한 내용은 매뉴얼을 참고하라.
 */

/* @lock
 * 세그먼트 셀렉터 MSR.
 */
#define MSR_STAR 0xc0000081
/* @lock
 * Long mode SYSCALL 대상.
 */
#define MSR_LSTAR 0xc0000082
/* @lock
 * eflags용 마스크.
 */
#define MSR_SYSCALL_MASK 0xc0000084

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* @lock
	 * 인터럽트 서비스 루틴은 syscall_entry가 유저랜드 스택을 커널 모드 스택으로
	 * 교체하기 전까지 어떤 인터럽트도 처리해서는 안 된다. 따라서 FLAG_FL을
	 * 마스킹했다.
	 */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* @lock
 * 메인 시스템 콜 인터페이스.
 */
void
syscall_handler (struct intr_frame *f UNUSED) {
	/* @lock
	 * TODO: 여기에 구현을 작성하라.
	 */
	printf ("system call!\n");
	thread_exit ();
}
