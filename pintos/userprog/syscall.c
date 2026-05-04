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

	/* @bookmark [6] filesys_lock 초기화
	 * 추가: syscall_init에서 lock_init(&filesys_lock) 호출
	 * 출처: 08db0db (args-none 테스트 통과) */
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
	default:
		break;
	}
}

/* @bookmark [11] halt: power_off로 시스템 종료
 * 추가: power_off() 호출
 * 출처: 08db0db (args-none 테스트 통과) */
void
halt (void) {
	power_off ();
}

/* @bookmark [10] exit: 종료 메시지 출력 후 thread_exit
 * 추가: printf("%s: exit(%d)") → thread_exit() (process_exit와 역할 분담 필요)
 * 출처: 08db0db (args-none 테스트 통과) */
void
exit (int status) {
	printf ("%s: exit(%d)\n", thread_current ()->name, status);
	thread_exit ();
}

/* @note dfdfdfdfdddfdfdfd */

/* @bookmark [9] write: stdout 출력 (fd==1), filesys_lock으로 동기화
 * 추가: check_address → lock_acquire → putbuf → lock_release
 * 출처: 08db0db (args-none 테스트 통과) */
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

/* @bookmark [8] check_address: 유저 포인터 유효성 검사
 * 추가: NULL 검사 + is_user_vaddr 커널 영역 침범 검사
 * 출처: 08db0db (args-none 테스트 통과) */
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
