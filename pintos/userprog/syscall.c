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

void syscall_entry (void);
void syscall_handler (struct intr_frame *);


/* 콜 함수들 */
void halt(void);
void exit(int status);
int write(int fd, const void *buffer, unsigned size);
bool create(const char *file, unsigned initial_size);
int open (const char *file);

/* 헬퍼 함수들*/
void check_address(void *addr);

/* 추가 변수들 */
struct lock filesys_lock;

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

	/* 추가 초기화 */
	lock_init(&filesys_lock);
}

/* @lock
 * 메인 시스템 콜 인터페이스.
 */
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_WRITE:
		/* fd, buffer, size를 전달받음 */
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_EXIT:
		/* 종료 상태값 받음 */
		exit(f->R.rdi);
		break;
	case SYS_CREATE:
		/* 파일 생성하기 */
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_OPEN:
		/* 해당 파일 열기 */
		f->R.rax = open(f->R.rdi);
		break;
	default:	
		break;
	}
}

/* 구현 명령어들 */
/* 시스템 전체 셧다운 */
void 
halt(void){
	power_off();
}

/* 종료: 출력 로그 찍기 */
void 
exit(int status){
	printf ("%s: exit(%d)\n", thread_current()->name, status);
	thread_exit();
}

/* 쓰기: 화면이든 파일이든 저장하고 뿌림 */
int 
write(int fd, const void *buffer, unsigned size){
	int write_result;

	/* 유효성 검사 로직 */
	check_address(buffer);

	/* 락 획득: 동시에 읽어서 꼬임 방지*/
	lock_acquire(&filesys_lock);
	
	/* 명령: 화면에 출력하라 */
	if(fd == 1){
		putbuf(buffer, size);
		write_result = size;
	}
	/* 명령: 기타... */
	else{
		/* 임시로 실패 처리 */
		write_result = -1;
	}

	/* 락 해제: 이제 읽을 필요 없음 */
	lock_release(&filesys_lock);

	return write_result;
}

/* 생성 함수: file 생성 및 공간을 할당한다 */
bool 
create(const char *file, unsigned initial_size){
	/* 에러로 인한 exit(-1) 반환을 위해 validation 추가 */
	check_address(file);
	return filesys_create(file, initial_size);
}

/* 열기 함수: fd 발급, list에 삽입 */
int 
open (const char *file){
	check_address(file);
	/* 현재 스레드 구하고 여기에 fd 넣음 */
	struct thread *curr = thread_current();
	int fd = filesys_open(file);
	/* 예외 처리 */
	if(fd == NULL) return -1;

	list_push_back(&curr->file_fd_list, &curr->ff_elem);
	return fd;
}

/* 여기서부턴 헬퍼 함수 기술 */
/* 유효성 검사 */
void check_address(void *addr){
	struct thread *curr = thread_current();
	/* 애초에 없다면? */
	if(addr == NULL){
		exit(-1);
	}
	
	/* 커널 영역 침범 여부 */
	if(!is_user_vaddr(addr)){
		exit(-1);
	}

	/* 해당 값이 해당 메모리 주소에 쓰여져 있는지 확인 */
	if(pml4_get_page(curr->pml4, addr) == NULL){
		exit(-1);
	}
}