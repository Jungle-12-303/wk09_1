#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

/* 추가 임포트 */
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 추가 함수들 */
void halt (void);
void exit (int status);
int write (int fd, const void *buffer, unsigned size);
void check_address (void *addr);

struct lock filesys_lock;

/*
 * 시스템 콜.
 *
 * 이전에는 시스템 콜 서비스가 인터럽트 핸들러
 * (예: Linux의 int 0x80)로 처리되었다. 하지만 x86-64에서는
 * 제조사가 `syscall` 명령어라는 더 효율적인 시스템 콜 요청 경로를 제공한다.
 *
 * syscall 명령어는 모델 전용 레지스터(MSR)에 저장된 값을 읽어 동작한다.
 * 자세한 내용은 매뉴얼을 참고하라.
 */

/*
 * 세그먼트 셀렉터 MSR.
 */
#define MSR_STAR 0xc0000081
/*
 * 롱 모드에서 사용할 SYSCALL 대상.
 */
#define MSR_LSTAR 0xc0000082
/*
 * eflags에 적용할 마스크.
 */
#define MSR_SYSCALL_MASK 0xc0000084

void
syscall_init (void) {
	write_msr (MSR_STAR, ((uint64_t) SEL_UCSEG - 0x10) << 48 |
	                             ((uint64_t) SEL_KCSEG) << 32);
	write_msr (MSR_LSTAR, (uint64_t) syscall_entry);

	/*
	 * 인터럽트 서비스 루틴은 syscall_entry가 유저랜드 스택을 커널 모드
	 * 스택으로 교체하기 전까지 어떤 인터럽트도 처리해서는 안 된다. 따라서
	 * FLAG_FL을 마스킹했다.
	 */
	write_msr (MSR_SYSCALL_MASK,
	           FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init (&filesys_lock);
}

/*
 * 메인 시스템 콜 인터페이스.
 */
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax) {
	case SYS_HALT:
		halt ();
		break;
	case SYS_WRITE:
		/* fd, buffer, size를 전달받는다. */
		f->R.rax = write (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_EXIT:
		/* 종료 상태값 받음 */
		exit (f->R.rdi);
		break;
	case SYS_FORK:
		fork(f->R.rdi,f);
		break;
	default:
		break;
	}
}

void
halt (void) {
	power_off ();
}

void
exit (int status) {
	printf ("%s: exit(%d)\n", thread_current ()->name, status);
	thread_exit ();
}

int
write (int fd, const void *buffer, unsigned size) {
	int write_result;

	/* 유효성 검사 로직 */
	check_address (buffer);

	/* 락 획득: 동시에 읽어서 꼬임 방지*/
	lock_acquire (&filesys_lock);

	/* 명령: 화면에 출력하라 */
	if (fd == 1) {
		putbuf (buffer, size);
		write_result = size;
	}
	/* 명령: 기타... */
	else {
		/* 임시로 실패 처리 */
		write_result = -1;
	}

	/* 락 해제: 이제 읽을 필요 없음 */
	lock_release (&filesys_lock);

	return write_result;
}

void
check_address (void *addr) {
	/* 애초에 없다면? */
	if (addr == NULL) {
		exit (-1);
	}

	/* 커널 영역 침범 여부 */
	if (!is_user_vaddr (addr)) {
		exit (-1);
	}
}

int
fork(const char *thread_name, struct intr_frame *f){
	process_fork(thread_name, f);
}
