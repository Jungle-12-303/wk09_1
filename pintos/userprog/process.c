#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/cstr.h"
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

struct fork_args {
	struct thread *parent;        // fork를 호출한 부모 스레드
	struct intr_frame if_;        // 부모의 유저 레지스터 스냅샷
	struct child_status *cs;      // 부모가 만든 자식 상태 레코드
};

/* fd_table 최대 슬롯 수 (4KB 페이지 / 포인터 크기). */
#define FD_MAX (PGSIZE / sizeof (struct file *))

/*
 * initd와 다른 프로세스를 위한 일반적인 프로세스 초기화 함수.
 */
/* @todo
 * 프로세스를 위한 초기화 함수 (미구현)
 */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* @bookmark
 * process_create_initd - 프로세스 시작
 */
tid_t
process_create_initd (const char *file_name) {
	char *file_name_copy;
	char program_name[THREAD_NAME_MAX];
	char *arg_start;
	tid_t tid;

	/*
	 * 커널 풀에서 페이지 메모리 할당, 0 = 커널 영역(유저 옵션 없음)
	 */
	file_name_copy = palloc_get_page (0);
	if (file_name_copy == NULL)
		return TID_ERROR;
	strlcpy (file_name_copy, file_name, PGSIZE);

	strlcpy (program_name, file_name, sizeof program_name);
	arg_start = strchr (program_name, ' ');
	if (arg_start != NULL)
		*arg_start = '\0';

	/*
	 * 메모리 첫번째 공백까지의 문자열을 스레드 이름으로 사용, 위에서 할당한
	 * 커널 페이지 주소(새 스레드 시작 함수 인자) 전달
	 */

	tid = thread_create (program_name, PRI_DEFAULT, initd, file_name_copy);
	if (tid == TID_ERROR)
		palloc_free_page (file_name_copy);
	return tid;
}

/*
 * 첫 번째 유저 프로세스를 시작하는 스레드 함수.
 */
/* @bookmark
 * initd - 유저 프로세스를 시작
 */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC ("Fail to launch initd\n");
	NOT_REACHED ();
}

/*
 * 현재 프로세스를 `name`이라는 이름으로 복제한다.
 * 새 프로세스의 스레드 id를 반환하고, 스레드를 생성할 수 없으면
 * TID_ERROR를 반환한다.
 */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	struct thread *curr = thread_current ();
	struct child_status *cs;
	tid_t tid;

	cs = malloc (sizeof *cs);
	if (cs == NULL)
		return TID_ERROR;

	cs->tid = TID_ERROR;
	cs->exit_status = -1;
	cs->waited = false;
	cs->exited = false;
	sema_init (&cs->wait_sema, 0);

	list_push_back (&curr->child_status_list, &cs->elem);

	tid = thread_create (name, PRI_DEFAULT, __do_fork, thread_current ());
	if (tid == TID_ERROR) {
		list_remove (&cs->elem);
		free (cs);
	}
	return tid;
}

#ifndef VM
/*
 * 이 함수를 pml4_for_each에 넘겨 부모의 주소 공간을 복제한다.
 * 이는 project 2에서만 사용된다.
 */
/* duplicate_pte - pml4_for_each 콜백. 부모의 PTE 하나를 자식에게 복제한다.
 *
 * 매개변수:
 *   pte - 부모의 페이지 테이블 엔트리 포인터
 *   va  - 이 PTE가 매핑하는 가상 주소
 *   aux - 부모 struct thread 포인터 (pml4_for_each에서 전달) */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* @todo
	 * 1. 커널 페이지 필터링:
	 * is_kernel_vaddr(va)이면 return true로 건너뛴다.
	 * 커널 영역은 모든 프로세스가 공유하므로 복제 대상이 아니다. */

	/* 2. 부모의 페이지 맵 레벨 4에서 VA를 해석한다. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* @todo
	 * 3. 자식용 새 페이지 할당:
	 * palloc_get_page(PAL_USER)로 유저 영역 페이지 1개를 할당한다.
	 * 실패하면 return false (메모리 부족, __do_fork의 error로 점프). */

	/* @todo
	 * 4. 부모 페이지 복사 + 쓰기 권한 확인:
	 * memcpy(newpage, parent_page, PGSIZE)로 4KB 전체를 복사한다.
	 * writable = is_writable(pte)로 부모 PTE의 쓰기 비트를 읽는다.
	 * (include/threads/mmu.h에 매크로 정의됨) */

	/* 5. 자식 페이지 테이블에 매핑 추가. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* @todo
		 * 6. 실패 시 방금 할당한 newpage를 palloc_free_page로 해제하고
		 * return false 한다. */
	}
	return true;
}
#endif

/*
 * 부모의 실행 문맥을 복사하는 스레드 함수.
 * 힌트) parent->tf에는 프로세스의 유저랜드 문맥이 들어 있지 않다.
 * 즉, process_fork의 두 번째 인자를 이 함수에 전달해야 한다.
 */
/* __do_fork - 자식 스레드의 진입점. 부모의 실행 문맥을 복제한다.
 *
 * 핵심 순서:
 *   1. 부모의 인터럽트 프레임(레지스터) 복사
 *   2. 부모의 페이지 테이블 복제 (duplicate_pte)
 *   3. 부모의 fd_table 복제 (file_duplicate)
 *   4. 자식의 fork 반환값을 0으로 설정
 *   5. 부모에게 "복제 완료" 알림, do_iret으로 유저 모드 진입 */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();

	/* @todo
	 * parent_if 전달 문제 해결:
	 * process_fork()의 if_ (유저랜드 레지스터 스냅샷)를 여기로 전달해야 한다.
	 * parent->tf는 커널 문맥이므로 쓸 수 없다.
	 *
	 * 방법: process_fork에서 if_를 parent의 멤버에 저장하고 여기서 읽는다.
	 * 예) thread.h에 struct intr_frame parent_if 필드를 추가하거나,
	 *     process_fork에서 memcpy(&parent->tf, if_)로 임시 저장한 뒤
	 *     여기서 parent->tf를 읽는다.
	 *
	 * 현재: parent_if가 초기화되지 않아 memcpy가 크래시한다. */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. CPU 문맥(레지스터)을 로컬 스택으로 복사한다. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. 페이지 테이블을 복제한다. */
	current->pml4 = pml4_create ();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* @todo
	 * 3. 부모의 fd_table 복제:
	 * parent->fd_table을 순회하며(i = 2 ~ FD_MAX) NULL이 아닌 슬롯마다
	 * file_duplicate(parent->fd_table[i])로 복제하여
	 * current->fd_table[i]에 저장한다.
	 * current->next_fd = parent->next_fd로 동기화한다.
	 *
	 * file_duplicate는 inode 참조 카운트를 증가시키는 함수다.
	 * 단순히 포인터를 복사하면 부모/자식이 같은 file 객체를 공유하게 되어
	 * 한쪽이 close하면 다른 쪽도 닫혀버린다. */

	/* @todo
	 * 4. 자식의 fork 반환값 설정:
	 * if_.R.rax = 0 으로 설정한다.
	 * fork()는 부모에게 자식 tid를, 자식에게 0을 반환해야 한다.
	 * 부모의 반환값은 process_fork가 thread_create의 tid를 반환하므로 자동.
	 * 자식은 여기서 rax를 0으로 덮어써야 한다. */

	/* @todo
	 * 5. 자식의 child_list 재초기화:
	 * memcpy로 부모의 메모리를 복사하면 child_list 포인터까지 복사된다.
	 * 자식은 자기만의 빈 child_list를 가져야 하므로
	 * list_init(&current->child_list)로 재초기화한다.
	 * parent 포인터도 이미 thread_create에서 설정되어 있으므로 확인만 한다. */

	/* @todo
	 * 6. 부모-자식 동기화 (fork 완료 알림):
	 * 힌트의 핵심: "이 함수가 부모의 자원을 성공적으로 복제하기 전까지
	 * 부모는 fork()에서 반환되면 안 된다."
	 *
	 * 방법: 세마포어를 하나 추가한다 (예: fork_sema).
	 *   - process_fork에서 thread_create 후 sema_down(&curr->fork_sema)
	 *   - __do_fork에서 복제 완료 후 sema_up(&parent->fork_sema)
	 *   - 실패(error) 시에도 sema_up 해서 부모가 깨어나야 한다.
	 *   - 부모는 깨어난 후 성공/실패를 확인하여 tid 또는 TID_ERROR 반환. */

	process_init ();

	/* 새로 생성된 프로세스로 전환한다. */
	if (succ)
		do_iret (&if_);
error:
	/* @todo
	 * 실패 시 처리:
	 * succ = false 설정 후, 부모에게 실패를 알려야 한다.
	 * (fork_sema를 사용한다면 여기서도 sema_up 필요) */
	thread_exit ();
}

/*
 * 현재 실행 문맥을 f_name으로 전환한다.
 * 실패하면 -1을 반환한다.
 */
/* @bookmark
 * process_exec - 바이너리 로드 후 유저 모드 전환
 */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/*
	 * thread 구조체에 있는 intr_frame은 사용할 수 없다.
	 * 현재 스레드가 다시 스케줄될 때 실행 정보를 그 멤버에 저장하기 때문이다.
	 */
	/*
	 * cs, ss, ds, es는 유저 모드
	 * eflags는 인터럽트 허용
	 */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/*
	 * 먼저 현재 문맥을 제거한다.
	 */
	/* @todo
	 * 이전 주소 공간 초기화 (미구현)
	 */
	process_cleanup ();

	/*
	 * 그리고 바이너리를 로드한다.
	 */
	/*
	 * 바이너리 세팅
	 */
	success = load (file_name, &_if);
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/*
	 * 전환된 프로세스를 시작한다.
	 */
	/*
	 * 프로세스를 시작
	 */
	do_iret (&_if);
	NOT_REACHED ();
}

/*
 * 스레드 TID가 죽을 때까지 기다린 뒤 그 종료 상태를 반환한다.
 * 커널에 의해 종료되었다면(즉 예외 때문에 죽었다면) -1을 반환한다.
 * TID가 유효하지 않거나, 호출한 프로세스의 자식이 아니거나,
 * 해당 TID에 대해 process_wait()가 이미 성공적으로 호출된 적이 있다면
 * 기다리지 않고 즉시 -1을 반환한다.
 *
 * 이 함수는 문제 2-2에서 구현될 것이다. 현재는 아무 일도 하지 않는다.
 */
/* @bookmark
 * process_wait: timer_sleep 임시 대기
 */
int
process_wait (tid_t child_tid UNUSED) {
	/*
	 * XXX: 힌트) process_wait(initd)를 호출하면 pintos가 종료된다.
	 * XXX: 따라서 process_wait를 구현하기 전에 여기에 무한 루프를 추가하는
	 * 것을
	 * XXX: 권장한다.
	 */
	// struct thread *child = thread_current ();
	timer_sleep (300);
	// sema_down(&child->wait_sema);
	// int status = child->exit_status;
	return -1;
}

/*
 * 프로세스를 종료한다. 이 함수는 thread_exit()에 의해 호출된다.
 */
void
process_exit (void) {
	struct thread *curr = thread_current ();

	/* @todo
	 * 열린 파일 전부 닫기:
	 * fd_table을 2번부터 FD_MAX까지 순회하며
	 * NULL이 아닌 슬롯은 file_close() 호출 후 NULL로 비운다. */

	/* @todo
	 * fd_table 페이지 해제:
	 * palloc_free_page(curr->fd_table)로 반환한다. */

	/* @todo
	 * 고아 해방 루프:
	 * child_list를 순회하며 wait하지 않은 자식들의
	 * exit_sema를 sema_up해서 소멸을 허가한다. */

	/* @todo
	 * 부모에게 종료 알림 (유저 프로세스만):
	 * if (curr->pml4 != NULL)
	 *   sema_up(&curr->wait_sema) -- 부모 깨우기
	 *   sema_down(&curr->exit_sema) -- 부모 수거 대기 */

	process_cleanup ();
}

/* @todo
 * process_add_file - 파일을 fd_table에 등록하고 fd 번호를 반환한다.
 *
 * 구현해야 할 것:
 * 1. curr->next_fd가 FD_MAX 이상이면 -1 반환 (테이블 꽉 참).
 * 2. curr->fd_table[curr->next_fd] = f 로 저장.
 * 3. curr->next_fd를 반환값으로 쓰고, next_fd++ 증가.
 * 4. 반환한 fd 번호를 돌려준다.
 *
 * 주의: 중간에 close로 빈 슬롯이 생기면 next_fd 방식은
 * 그 빈 자리를 재활용하지 못한다. 단순 구현에서는 괜찮지만,
 * multi-oom 테스트를 통과하려면 빈 슬롯 탐색이 필요할 수 있다. */
int
process_add_file (struct file *f) {
	struct thread *curr = thread_current ();
	(void) curr;
	(void) f;
	return -1;
}

/* @todo
 * process_get_file - fd 번호로 파일 포인터를 반환한다.
 *
 * 구현해야 할 것:
 * 1. fd가 범위 밖이면 (fd < 0 || fd >= FD_MAX) NULL 반환.
 * 2. curr->fd_table[fd]를 반환한다 (NULL이면 열리지 않은 fd). */
struct file *
process_get_file (int fd) {
	struct thread *curr = thread_current ();
	(void) curr;
	(void) fd;
	return NULL;
}

/* @todo
 * process_close_file - fd를 닫고 fd_table에서 제거한다.
 *
 * 구현해야 할 것:
 * 1. fd가 범위 밖이면 (fd < 2 || fd >= FD_MAX) 무시 (0,1은 예약).
 * 2. curr->fd_table[fd]가 NULL이면 무시.
 * 3. file_close(curr->fd_table[fd]) 호출.
 * 4. curr->fd_table[fd] = NULL로 비운다. */
void
process_close_file (int fd) {
	struct thread *curr = thread_current ();
	(void) curr;
	(void) fd;
}

/*
 * 현재 프로세스의 자원을 해제한다.
 */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/*
	 * 현재 프로세스의 페이지 디렉터리를 파괴하고 커널 전용 페이지 디렉터리로
	 * 되돌린다.
	 */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/*
		 * 여기서 순서는 매우 중요하다. 페이지 디렉터리를 전환하기 전에
		 * cur->pagedir를 NULL로 설정해야 타이머 인터럽트가 다시 프로세스의
		 * 페이지 디렉터리로 되돌아가지 않는다. 또한 프로세스의 페이지
		 * 디렉터리를 파괴하기 전에 기본 페이지 디렉터리를 활성화해야 한다.
		 * 그렇지 않으면 현재 활성 페이지 디렉터리가 이미 해제되어
		 * 비워진 것이 되어 버린다.
		 */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/*
 * 다음 스레드에서 유저 코드를 실행하도록 CPU를 설정한다.
 * 이 함수는 문맥 전환이 일어날 때마다 호출된다.
 */
void
process_activate (struct thread *next) {
	pml4_activate (next->pml4);
	tss_update (next);
}

/*
 * 우리는 ELF 바이너리를 로드한다. 다음 정의들은 ELF 명세 [ELF1]에서
 * 거의 그대로 가져온 것이다.
 */

/*
 * ELF 타입들. [ELF1] 1-2를 참고하라.
 */
#define EI_NIDENT 16

/*
 * 무시한다.
 */
#define PT_NULL 0
/*
 * 로드 가능한 세그먼트.
 */
#define PT_LOAD 1
/*
 * 동적 링크 정보.
 */
#define PT_DYNAMIC 2
/*
 * 동적 로더의 이름.
 */
#define PT_INTERP 3
/*
 * 보조 정보.
 */
#define PT_NOTE 4
/*
 * 예약됨.
 */
#define PT_SHLIB 5
/*
 * 프로그램 헤더 테이블.
 */
#define PT_PHDR 6
/*
 * 스택 세그먼트.
 */
#define PT_STACK 0x6474e551

/*
 * 실행 가능.
 */
#define PF_X 1
/*
 * 쓰기 가능.
 */
#define PF_W 2
/*
 * 읽기 가능.
 */
#define PF_R 4

/*
 * 실행 파일 헤더. [ELF1] 1-4부터 1-8까지를 참고하라.
 * 이는 ELF 바이너리의 맨 앞에 위치한다.
 */
/*
 * ELF64 헤더는 실행 파일의 맨 앞 64바이트에 위치한다.
 * 이 영역은 파일 전체를 읽기 전에 먼저 확인하는 요약 정보다.
 *
 * 바이트 배치는 다음과 같다.
 * 0x00 ~ 0x0f : e_ident
 *               ELF 매직 값, 64비트 형식, 엔디안 같은 기본 식별 정보
 * 0x10 ~ 0x11 : e_type
 *               실행 파일인지, 목적 파일인지 같은 파일 종류
 * 0x12 ~ 0x13 : e_machine
 *               대상 CPU 종류, 여기서는 x86-64(0x3E)
 * 0x14 ~ 0x17 : e_version
 *               ELF 버전
 * 0x18 ~ 0x1f : e_entry
 *               적재가 끝난 뒤 처음 실행할 가상 주소
 * 0x20 ~ 0x27 : e_phoff
 *               프로그램 헤더 표가 파일에서 시작하는 위치
 * 0x28 ~ 0x2f : e_shoff
 *               섹션 헤더 표가 파일에서 시작하는 위치
 * 0x30 ~ 0x33 : e_flags
 *               추가 플래그
 * 0x34 ~ 0x35 : e_ehsize
 *               이 ELF 헤더 자체의 크기
 * 0x36 ~ 0x37 : e_phentsize
 *               프로그램 헤더 1개의 크기
 * 0x38 ~ 0x39 : e_phnum
 *               프로그램 헤더 개수
 * 0x3a ~ 0x3b : e_shentsize
 *               섹션 헤더 1개의 크기
 * 0x3c ~ 0x3d : e_shnum
 *               섹션 헤더 개수
 * 0x3e ~ 0x3f : e_shstrndx
 *               섹션 이름 문자열 표의 인덱스
 */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/*
 * 약어.
 */
#define ELF  ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/*
 * FILE_NAME의 ELF 실행 파일을 현재 스레드에 로드한다.
 * 실행 파일의 진입점을 *RIP에 저장하고,
 * 초기 스택 포인터를 *RSP에 저장한다.
 * 성공하면 true, 그렇지 않으면 false를 반환한다.
 */
/* @bookmark
 * load - 실행 파일의 진입점
 */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	/*
	 * ELF : 현재 실행 파일의 ELF64 헤더 저장 변수
	 */
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;
	char *argv[64];
	char *token, *save_point;
	char *fn_copy = NULL;
	int argc = 0;

	/*
	 * 페이지 디렉터리를 할당하고 활성화한다.
	 */
	/*
	 * 커널 주소 공간 매핑이 포함된 현재 프로세스용 페이지 테이블 생성
	 * 생성한 페이지 테이블을 현재 CPU에 반영
	 */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (t);

	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return false;
	/* @breakpoint
	 * (cahr *) file_name
	 */
	memcpy (fn_copy, file_name, CSTR_SIZE (file_name));

	/*
	 * 원본 문자열을 잘라가며 토큰 생성
	 */
	token = strtok_r (fn_copy, " ", &save_point);
	while (token != NULL) {
		if ((size_t) argc >= sizeof argv / sizeof *argv)
			goto done;
		argv[argc++] = token;
		token = strtok_r (NULL, " ", &save_point);
	}

	if (argc == 0)
		goto done;

	/*
	 * 실행 파일을 연다.
	 */
	file = filesys_open (argv[0]);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/*
	 * 실행 파일 헤더를 읽고 검증한다.
	 */
	/* @region ELF 헤더 검사 - 실행 가능한 ELF 파일 형식 확인 */
	/*
	 * e_ident    : ELF 파일 여부, 64비트 여부, 엔디안 정보
	 *              \177ELF = ELF 매직 값
	 *              \2      = 64비트 ELF
	 *              \1      = little-endian
	 *              \1      = ELF 버전 1
	 * e_type     : 파일 종류, 2 = 실행 파일
	 *              1 = relocatable, 2 = executable, 3 = shared object
	 * e_machine  : 대상 CPU 종류, 0x3E = x86-64
	 * e_version  : ELF 버전, 1 = 현재 버전
	 * e_phentsize: 프로그램 헤더 1개의 크기
	 * e_phnum    : 프로그램 헤더 개수, 너무 크면 비정상 파일로 간주
	 */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr ||
	    memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 ||
	    ehdr.e_machine != 0x3E || ehdr.e_version != 1 ||
	    ehdr.e_phentsize != sizeof (struct Phdr) || ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}
	/* @endregion */

	/*
	 * 프로그램 헤더들을 읽는다.
	 */
	/* @region 프로그램 헤더 표 순회 - 메모리에 올릴 파일 구역 설명서 읽기 */
	/*
	 * e_phoff : 파일 시작 기준 프로그램 헤더 표 시작 위치
	 * e_phnum : 프로그램 헤더 개수
	 *
	 * 프로그램 헤더 1개는
	 * "파일의 어느 구역을 메모리 어디에 어떤 권한으로 올릴지"를 설명한다.
	 */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		/*
		 * file_ofs 범위 검사
		 * file_seek 이후의 file_read는 file_ofs 위치부터 읽는다.
		 */
		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		/*
		 * 현재 프로그램 헤더 1개를 읽고 다음 헤더 위치로 이동
		 */
		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;

		/*
		 * PT_LOAD   : 실제 메모리에 올릴 파일 구역
		 * PT_NULL   : 비어 있는 항목
		 * PT_NOTE   : 부가 정보
		 * PT_PHDR   : 프로그램 헤더 표 자체 설명
		 * PT_STACK  : 스택 관련 정보
		 * PT_DYNAMIC, PT_INTERP, PT_SHLIB : 현재 로더에서 지원하지 않음
		 */
		switch (phdr.p_type) {
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/*
			 * 이 세그먼트는 무시한다.
			 */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			/*
			 * validate_segment : 이 구역을 실제로 적재 가능한지 검사
			 * p_flags          : 쓰기 가능 여부 확인
			 * p_offset         : 파일 안에서 이 구역이 시작하는 위치
			 * p_vaddr          : 메모리에서 이 구역이 올라갈 가상 주소
			 *
			 * file_page   : 파일 쪽 페이지 시작 주소
			 * mem_page    : 메모리 쪽 페이지 시작 주소
			 * page_offset : 첫 페이지 안에서 실제 데이터가 시작하는 위치
			 */
			if (validate_segment (&phdr, file)) {
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;

				/*
				 * p_filesz > 0
				 * 파일에 실제 데이터가 있는 구역
				 * 처음 부분은 디스크에서 읽고 남는 메모리 공간은 0으로 채움
				 *
				 * p_filesz == 0
				 * 파일에는 데이터가 없고 메모리만 필요한 구역
				 * 예: BSS
				 * 전부 0으로 채움
				 */
				if (phdr.p_filesz > 0) {
					/*
					 * 일반 세그먼트.
					 * 처음 부분은 디스크에서 읽고 나머지는 0으로 채운다.
					 */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes =
					        (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE) -
					         read_bytes);
				} else {
					/*
					 * 전부 0인 세그먼트.
					 * 디스크에서 아무것도 읽지 않는다.
					 */
					read_bytes = 0;
					zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
				}

				/*
				 * file_page부터 read_bytes만큼 읽고
				 * 나머지 zero_bytes는 0으로 채운 페이지를 매핑
				 */
				if (!load_segment (file, file_page, (void *) mem_page,
				                   read_bytes, zero_bytes, writable))
					goto done;
			} else
				goto done;
			break;
		}
	}
	/* @endregion */

	/*
	 * 스택을 설정한다.
	 */
	/* @region 유저 스택 구성 - 인자 문자열, argv 배열, 시작 레지스터 준비 */
	if (!setup_stack (if_))
		goto done;

	char *stack_p = (char *) if_->rsp;

	/*
	 * 시작 주소.
	 */
	if_->rip = ehdr.e_entry;

	/*
	 * 스택의 아래 방향 성장
	 * 마지막 인자부터 역순 복사
	 *
	 * 예:
	 * argv[0] = "echo"
	 * argv[1] = "x"
	 * argv[2] = "y"
	 *
	 * 복사 순서:
	 * "y" -> "x" -> "echo"
	 *
	 * 복사 후:
	 * argv[0] = 유저 스택 안 "echo" 시작 주소
	 * argv[1] = 유저 스택 안 "x" 시작 주소
	 * argv[2] = 유저 스택 안 "y" 시작 주소
	 */
	for (int argi = argc - 1; argi >= 0; argi--) {
		int size = CSTR_SIZE (argv[argi]);
		stack_p -= size;
		memcpy (stack_p, argv[argi], size);
		argv[argi] = stack_p;
	}

	/*
	 * 예:
	 * stack_p = 0x...f7 -> 0x...f0
	 * 이후 적재 값의 8바이트 포인터 단위
	 */
	stack_p = (char *) ((uintptr_t) stack_p & -8);

	/*
	 * argv[argc] = NULL 자리
	 */
	stack_p -= 8;
	memset (stack_p, 0, 8);
	/*
	 * 문자열 포인터 배열의 끝 표시
	 *
	 * 예:
	 * argv[0] = "echo"
	 * argv[1] = "x"
	 * argv[2] = "y"
	 * argv[3] = NULL
	 */

	/*
	 * 앞 단계의 argv[i] = 유저 스택 안 문자열 주소
	 * 해당 주소값들의 8바이트 단위 재적재
	 *
	 * 예:
	 * argv[0] = 0x473ff7 -> "echo"
	 * argv[1] = 0x473ffc -> "x"
	 * argv[2] = 0x473ffe -> "y"
	 *
	 * 스택 기록 값:
	 * [0x473ff7] [0x473ffc] [0x473ffe] [NULL]
	 */
	for (int n = argc - 1; n >= 0; n--) {
		stack_p -= 8;
		*(uintptr_t *) stack_p = (uintptr_t) argv[n];
	}

	/*
	 * rdi : argc
	 * rsi : argv 배열 시작 주소
	 */
	if_->R.rsi = stack_p;
	if_->R.rdi = argc;

	stack_p -= 8;
	memset (stack_p, 0, 8);
	if_->rsp = stack_p;
	/*
	 * if_->rsp = 유저 프로그램 시작 시 사용할 스택 포인터
	 */
	/* @endregion */

	// hex_dump(if_->rsp, if_->rsp, USER_STACK - (uint64_t)if_->rsp, true);
	success = true;

done:
	/*
	 * load의 성공 여부와 관계없이 여기로 도착
	 * 메모리 해제 진행
	 */
	if (fn_copy != NULL)
		palloc_free_page (fn_copy);

	if (file != NULL)
		file_close (file);
	return success;
}

#ifndef VM
/*
 * 이 블록의 코드는 project 2 동안에만 사용된다.
 * project 2 전체를 위한 함수를 구현하고 싶다면 #ifndef 매크로 바깥에 구현하라.
 */

/*
 * load() 보조 함수들.
 */
static bool install_page (void *upage, void *kpage, bool writable);

/*
 * FILE의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 로드한다.
 * 전체적으로 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 아래와 같이
 * 초기화된다.
 *
 * - UPAGE의 READ_BYTES 바이트는 FILE의 OFS 오프셋부터 읽어 와야 한다.
 *
 * - UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 채워야 한다.
 *
 * 이 함수가 초기화한 페이지들은 WRITABLE이 true면 유저 프로세스가
 * 쓸 수 있어야 하고, 그렇지 않으면 읽기 전용이어야 한다.
 *
 * 성공하면 true를, 메모리 할당 오류나 디스크 읽기 오류가 발생하면 false를
 * 반환한다.
 */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/*
		 * 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채운다.
		 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/*
		 * 메모리 페이지 하나를 얻는다.
		 */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/*
		 * 이 페이지를 로드한다.
		 */
		if (file_read (file, kpage, page_read_bytes) !=
		    (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/*
		 * 이 페이지를 프로세스의 주소 공간에 추가한다.
		 */
		if (!install_page (upage, kpage, writable)) {
			printf ("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/*
		 * 다음 페이지로 진행한다.
		 */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/*
 * USER_STACK에 0으로 채워진 페이지를 매핑해 최소한의 스택을 만든다.
 */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success =
		        install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/*
 * 유저 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로의 매핑을 페이지 테이블에
 * 추가한다.
 * WRITABLE이 true면 유저 프로세스가 이 페이지를 수정할 수 있고,
 * 그렇지 않으면 읽기 전용이다.
 * UPAGE는 이미 매핑되어 있으면 안 된다.
 * KPAGE는 아마도 palloc_get_page()로 유저 풀에서 얻은 페이지여야 한다.
 * 성공하면 true를, UPAGE가 이미 매핑되어 있거나 메모리 할당에 실패하면
 * false를 반환한다.
 */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/*
	 * 그 가상 주소에 이미 페이지가 없는지 확인한 뒤, 그 위치에 페이지를
	 * 매핑한다.
	 */
	return (pml4_get_page (t->pml4, upage) == NULL &&
	        pml4_set_page (t->pml4, upage, kpage, writable));
}

/* 구현이 안된 이 함수를 넣으라고 함 */
/* PHDR이 유효한 로드 가능한 세그먼트인지 확인하고 true를 반환합니다. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset과 p_vaddr은 동일한 페이지 오프셋을 가져야 합니다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 파일 내부에 있어야 합니다. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz는 p_filesz보다 크거나 같아야 합니다. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트는 가상 메모리의 유저 영역 내에 위치해야 합니다. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 주소 영역이 감싸지는(wrap around) 형태가 아니어야 합니다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 0번 페이지는 매핑하지 않습니다.
	   (NULL 포인터 역참조를 잡기 위해 유효하지 않은 주소로 둡니다.) */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* 괜찮은 것 같습니다. */
	return true;
}

#else
/*
 * 여기부터의 코드는 project 3 이후에 사용된다.
 * project 2만을 위한 함수를 구현하려면 위쪽 블록에 구현하라.
 */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/*
	 * TODO: 파일에서 세그먼트를 로드하라.
	 */
	/*
	 * TODO: 이 함수는 VA 주소에서 첫 번째 페이지 폴트가 발생했을 때 호출된다.
	 */
	/*
	 * TODO: 이 함수를 호출할 때 VA를 사용할 수 있다.
	 */
}

/*
 * FILE의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 로드한다.
 * 전체적으로 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 아래와 같이
 * 초기화된다.
 *
 * - UPAGE의 READ_BYTES 바이트는 FILE의 OFS 오프셋부터 읽어 와야 한다.
 *
 * - UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 채워야 한다.
 *
 * 이 함수가 초기화한 페이지들은 WRITABLE이 true면 유저 프로세스가
 * 쓸 수 있어야 하고, 그렇지 않으면 읽기 전용이어야 한다.
 *
 * 성공하면 true를, 메모리 할당 오류나 디스크 읽기 오류가 발생하면 false를
 * 반환한다.
 */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/*
		 * 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채운다.
		 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/*
		 * TODO: lazy_load_segment에 정보를 전달할 aux를 설정하라.
		 */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage, writable,
		                                     lazy_load_segment, aux))
			return false;

		/*
		 * 다음 페이지로 진행한다.
		 */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/*
 * USER_STACK에 스택용 PAGE를 만든다. 성공하면 true를 반환한다.
 */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/*
	 * TODO: stack_bottom에 스택을 매핑하고 페이지를 즉시 claim하라.
	 * TODO: 성공하면 그에 맞게 rsp를 설정하라.
	 * TODO: 해당 페이지를 스택 페이지로 표시해야 한다.
	 */
	/*
	 * TODO: 여기에 코드를 작성하라.
	 */

	return success;
}
#endif /* VM */
