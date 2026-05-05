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
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 추가 함수들 */
void halt (void);
void exit (int status);
int write (int fd, const void *buffer, unsigned size);
bool create (const char *file, unsigned initial_size);
int open (const char *file);
void close (int fd);
void check_address (const void *addr);

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
		f->R.rax = write (f->R.rdi, (const void *) f->R.rsi, f->R.rdx);
		break;
	case SYS_EXIT:
		/* 종료 상태값 받음 */
		exit (f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create ((const char *) f->R.rdi, f->R.rsi);
		break;
	case SYS_OPEN:
		f->R.rax = open ((const char *) f->R.rdi);
		break;
	case SYS_CLOSE:
		close (f->R.rdi);
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
	if (thread_current ()->self_status != NULL)
		thread_current ()->self_status->exit_status = status;
	thread_exit ();
}

int
write (int fd, const void *buffer, unsigned size) {
	int write_result;
	struct file *file;

	/* 유효성 검사 로직 */
	check_address (buffer);
	if (size > 0)
		check_address ((const char *) buffer + size - 1);

	/* 락 획득: 동시에 읽어서 꼬임 방지*/
	lock_acquire (&filesys_lock);

	/* 명령: 화면에 출력하라 */
	if (fd == 1) {
		putbuf (buffer, size);
		write_result = size;
	}
	/* 명령: 기타... */
	else {
		file = process_get_file (fd);
		write_result = file == NULL ? -1 : file_write (file, buffer, size);
	}

	/* 락 해제: 이제 읽을 필요 없음 */
	lock_release (&filesys_lock);

	return write_result;
}

bool
create (const char *file, unsigned initial_size) {
	bool result;

	check_address ((void *) file);
	lock_acquire (&filesys_lock);
	result = filesys_create (file, initial_size);
	lock_release (&filesys_lock);
	return result;
}

int
open (const char *file) {
	struct file *opened_file;
	int fd;

	check_address ((void *) file);
	lock_acquire (&filesys_lock);
	opened_file = filesys_open (file);
	if (opened_file == NULL) {
		lock_release (&filesys_lock);
		return -1;
	}

	fd = process_add_file (opened_file);
	if (fd == -1)
		file_close (opened_file);
	lock_release (&filesys_lock);
	return fd;
}

void
close (int fd) {
	lock_acquire (&filesys_lock);
	process_close_file (fd);
	lock_release (&filesys_lock);
}

void
check_address (const void *addr) {
	struct thread *curr = thread_current ();

	/* 애초에 없다면? */
	if (addr == NULL) {
		exit (-1);
	}

	/* 커널 영역 침범 여부 */
	if (!is_user_vaddr (addr)) {
		exit (-1);
	}

	if (curr->pml4 != NULL && pml4_get_page (curr->pml4, addr) == NULL)
		exit (-1);
}
