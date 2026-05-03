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

/* @lock
 * initd와 다른 프로세스를 위한 일반적인 프로세스 초기화 함수.
 */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

tid_t
process_create_initd (const char *file_name) {
	char *file_name_copy;
	char program_name[THREAD_NAME_MAX];
	char *arg_start;
	tid_t tid;

	file_name_copy = palloc_get_page (0);
	if (file_name_copy == NULL)
		return TID_ERROR;
	strlcpy (file_name_copy, file_name, PGSIZE);

	strlcpy (program_name, file_name, sizeof program_name);
	arg_start = strchr (program_name, ' ');
	if (arg_start != NULL)
		*arg_start = '\0';

	tid = thread_create (program_name, PRI_DEFAULT, initd, file_name_copy);
	if (tid == TID_ERROR)
		palloc_free_page (file_name_copy);
	return tid;
}

/* @lock
 * 첫 번째 유저 프로세스를 시작하는 스레드 함수.
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

/* @lock
 * 현재 프로세스를 `name`이라는 이름으로 복제한다.
 * 새 프로세스의 스레드 id를 반환하고, 스레드를 생성할 수 없으면
 * TID_ERROR를 반환한다.
 */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* @lock
	 * 현재 스레드를 새 스레드로 복제한다.
	 */
	return thread_create (name, PRI_DEFAULT, __do_fork, thread_current ());
}

#ifndef VM
/* @lock
 * 이 함수를 pml4_for_each에 넘겨 부모의 주소 공간을 복제한다.
 * 이는 project 2에서만 사용된다.
 */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* @lock
	 * TODO: parent_page가 커널 페이지라면 즉시 반환하라.
	 */

	/* @lock
	 * 2. 부모의 페이지 맵 레벨 4에서 VA를 해석한다.
	 */
	parent_page = pml4_get_page (parent->pml4, va);

	/* @lock
	 * TODO: 3. 자식을 위한 새 PAL_USER 페이지를 할당하고 결과를
	 * TODO: NEWPAGE에 저장하라.
	 */

	/* @lock
	 * TODO: 4. 부모의 페이지를 새 페이지로 복제하고,
	 * TODO: 부모 페이지가 쓰기 가능한지 확인하여 결과에 따라 WRITABLE을
	 * TODO: 설정하라.
	 */

	/* @lock
	 * 5. WRITABLE 권한으로 VA 주소에 자식의 페이지 테이블에 새 페이지를
	 * 추가한다.
	 */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* @lock
		 * TODO: 6. 페이지 삽입에 실패하면 오류 처리를 하라.
		 */
	}
	return true;
}
#endif

/* @lock
 * 부모의 실행 문맥을 복사하는 스레드 함수.
 * 힌트) parent->tf에는 프로세스의 유저랜드 문맥이 들어 있지 않다.
 * 즉, process_fork의 두 번째 인자를 이 함수에 전달해야 한다.
 */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* @lock
	 * TODO: 어떻게든 parent_if를 전달하라.
	 * TODO: 즉 process_fork()의 if_를 전달해야 한다.
	 */
	struct intr_frame *parent_if;
	bool succ = true;

	/* @lock
	 * 1. CPU 문맥을 로컬 스택으로 읽어 온다.
	 */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* @lock
	 * 2. 페이지 테이블을 복제한다.
	 */
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

	/* @lock
	 * TODO: 여기에 코드를 작성하라.
	 * TODO: 힌트) 파일 객체를 복제하려면 include/filesys/file.h의
	 * TODO: `file_duplicate`를 사용하라. 이 함수가 부모의 자원을 성공적으로
	 * TODO: 복제하기 전까지 부모는 fork()에서 반환되면 안 된다는 점에
	 * 주의하라.
	 */

	process_init ();

	/* @lock
	 * 마지막으로 새로 생성된 프로세스로 전환한다.
	 */
	if (succ)
		do_iret (&if_);
error:
	thread_exit ();
}

/* @lock
 * 현재 실행 문맥을 f_name으로 전환한다.
 * 실패하면 -1을 반환한다.
 */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* @lock
	 * thread 구조체에 있는 intr_frame은 사용할 수 없다.
	 * 현재 스레드가 다시 스케줄될 때 실행 정보를 그 멤버에 저장하기 때문이다.
	 */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* @lock
	 * 먼저 현재 문맥을 제거한다.
	 */
	process_cleanup ();

	/* @lock
	 * 그리고 바이너리를 로드한다.
	 */
	success = load (file_name, &_if);

	/* @lock
	 * load에 실패하면 종료한다.
	 */
	palloc_free_page (file_name);
	if (!success)
		return -1;

	/* @lock
	 * 전환된 프로세스를 시작한다.
	 */
	do_iret (&_if);
	NOT_REACHED ();
}

/* @lock
 * 스레드 TID가 죽을 때까지 기다린 뒤 그 종료 상태를 반환한다.
 * 커널에 의해 종료되었다면(즉 예외 때문에 죽었다면) -1을 반환한다.
 * TID가 유효하지 않거나, 호출한 프로세스의 자식이 아니거나,
 * 해당 TID에 대해 process_wait()가 이미 성공적으로 호출된 적이 있다면
 * 기다리지 않고 즉시 -1을 반환한다.
 *
 * 이 함수는 문제 2-2에서 구현될 것이다. 현재는 아무 일도 하지 않는다.
 */
int
process_wait (tid_t child_tid UNUSED) {
	/* @lock
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

/* @lock
 * 프로세스를 종료한다. 이 함수는 thread_exit()에 의해 호출된다.
 */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* @lock
	 * TODO: 여기에 코드를 작성하라.
	 * TODO: 프로세스 종료 메시지를 구현하라.
	 * TODO: project2/process_termination.html을 참고하라.
	 * TODO: 프로세스 자원 정리도 여기서 구현하는 것을 권장한다.
	 */

	process_cleanup ();
	// sema_up(&curr->wait_sema);
}

/* @lock
 * 현재 프로세스의 자원을 해제한다.
 */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* @lock
	 * 현재 프로세스의 페이지 디렉터리를 파괴하고 커널 전용 페이지 디렉터리로
	 * 되돌린다.
	 */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* @lock
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

/* @lock
 * 다음 스레드에서 유저 코드를 실행하도록 CPU를 설정한다.
 * 이 함수는 문맥 전환이 일어날 때마다 호출된다.
 */
void
process_activate (struct thread *next) {
	/* @lock
	 * 스레드의 페이지 테이블을 활성화한다.
	 */
	pml4_activate (next->pml4);

	/* @lock
	 * 인터럽트 처리에 사용할 스레드의 커널 스택을 설정한다.
	 */
	tss_update (next);
}

/* @lock
 * 우리는 ELF 바이너리를 로드한다. 다음 정의들은 ELF 명세 [ELF1]에서
 * 거의 그대로 가져온 것이다.
 */

/* @lock
 * ELF 타입들. [ELF1] 1-2를 참고하라.
 */
#define EI_NIDENT 16

/* @lock
 * 무시한다.
 */
#define PT_NULL 0
/* @lock
 * 로드 가능한 세그먼트.
 */
#define PT_LOAD 1
/* @lock
 * 동적 링크 정보.
 */
#define PT_DYNAMIC 2
/* @lock
 * 동적 로더의 이름.
 */
#define PT_INTERP 3
/* @lock
 * 보조 정보.
 */
#define PT_NOTE 4
/* @lock
 * 예약됨.
 */
#define PT_SHLIB 5
/* @lock
 * 프로그램 헤더 테이블.
 */
#define PT_PHDR 6
/* @lock
 * 스택 세그먼트.
 */
#define PT_STACK 0x6474e551

/* @lock
 * 실행 가능.
 */
#define PF_X 1
/* @lock
 * 쓰기 가능.
 */
#define PF_W 2
/* @lock
 * 읽기 가능.
 */
#define PF_R 4

/* @lock
 * 실행 파일 헤더. [ELF1] 1-4부터 1-8까지를 참고하라.
 * 이는 ELF 바이너리의 맨 앞에 위치한다.
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

/* @lock
 * 약어.
 */
#define ELF  ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* @lock
 * FILE_NAME의 ELF 실행 파일을 현재 스레드에 로드한다.
 * 실행 파일의 진입점을 *RIP에 저장하고,
 * 초기 스택 포인터를 *RSP에 저장한다.
 * 성공하면 true, 그렇지 않으면 false를 반환한다.
 */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* 적재용 변수 선언 */
	char *argv[64];
	char *temp, *save_point;
	char *fn_copy = NULL;
	int argc = 0;

	char *store_p[64];
	int j;

	/* @lock
	 * 페이지 디렉터리를 할당하고 활성화한다.
	 */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* 초기 설정:
	 * 데이터 오염이 없도록, fn_copy에 안전하게 복사 */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return false;

	memcpy (fn_copy, file_name, CSTR_SIZE (file_name));

	/* 이제 argv 배열 채우기 */
	temp = strtok_r (fn_copy, " ", &save_point);
	argv[argc] = temp;
	argc++;

	while (temp != NULL) {
		temp = strtok_r (NULL, " ", &save_point);
		if (temp != NULL) {
			argv[argc] = temp;
			argc++;
		}
	}

	j = argc - 1;

	/* @lock
	 * 실행 파일을 연다.
	 */
	file = filesys_open (argv[0]);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* @lock
	 * 실행 파일 헤더를 읽고 검증한다.
	 */
	/* amd64를 의미한다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr ||
	    memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 ||
	    ehdr.e_machine != 0x3E || ehdr.e_version != 1 ||
	    ehdr.e_phentsize != sizeof (struct Phdr) || ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* @lock
	 * 프로그램 헤더들을 읽는다.
	 */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* @lock
			 * 이 세그먼트는 무시한다.
			 */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			if (validate_segment (&phdr, file)) {
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0) {
					/* @lock
					 * 일반 세그먼트.
					 * 처음 부분은 디스크에서 읽고 나머지는 0으로 채운다.
					 */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes =
					        (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE) -
					         read_bytes);
				} else {
					/* @lock
					 * 전부 0인 세그먼트.
					 * 디스크에서 아무것도 읽지 않는다.
					 */
					read_bytes = 0;
					zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment (file, file_page, (void *) mem_page,
				                   read_bytes, zero_bytes, writable))
					goto done;
			} else
				goto done;
			break;
		}
	}

	/* @lock
	 * 스택을 설정한다.
	 */
	if (!setup_stack (if_))
		goto done;

	char *stack_p = (char *) if_->rsp;
	/* @lock
	 * 시작 주소.
	 */
	if_->rip = ehdr.e_entry;

	/* 스택의 현 포인터를 찾는다
	 * 쓸 대는 -8 하고, 읽을 때는 +8 함
	 * 따라서, 배열도 거꾸로 적어야 한다 */
	while (j >= 0) {
		int size = CSTR_SIZE (argv[j]);
		/* 포인터를 이동한 후 cpy 한다 */
		stack_p -= size;
		memcpy (stack_p, argv[j], size);
		/* 이후 주솟값을 사용해야 하므로 임시 저장 */
		store_p[j] = stack_p;
		j--;
	}

	/* 잘은 모르겠는데.. 이렇게 셋팅해야 한다고 */
	stack_p = (char *) ((uintptr_t) stack_p & -8);

	/* padding 하기 */
	stack_p -= 8;
	memset (stack_p, 0, 8);

	/* 포인터 주소 채우기 */
	for (int n = argc - 1; n >= 0; n--) {
		stack_p -= 8;
		*(uintptr_t *) stack_p = (uintptr_t) store_p[n];
	}

	/* 헤더 및 추가 주소값 설정 */
	if_->R.rsi = stack_p;
	if_->R.rdi = argc;

	stack_p -= 8;
	memset (stack_p, 0, 8);
	if_->rsp = stack_p;
	// hex_dump(if_->rsp, if_->rsp, USER_STACK - (uint64_t)if_->rsp, true);
	success = true;

done:
	/* @lock
	 * load의 성공 여부와 관계없이 여기로 도착한다.
	 */

	/* 메모리 해제 진행 */
	if (fn_copy != NULL)
		palloc_free_page (fn_copy);

	file_close (file);
	return success;
}

#ifndef VM
/* @lock
 * 이 블록의 코드는 project 2 동안에만 사용된다.
 * project 2 전체를 위한 함수를 구현하고 싶다면 #ifndef 매크로 바깥에 구현하라.
 */

/* @lock
 * load() 보조 함수들.
 */
static bool install_page (void *upage, void *kpage, bool writable);

/* @lock
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
		/* @lock
		 * 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채운다.
		 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* @lock
		 * 메모리 페이지 하나를 얻는다.
		 */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* @lock
		 * 이 페이지를 로드한다.
		 */
		if (file_read (file, kpage, page_read_bytes) !=
		    (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* @lock
		 * 이 페이지를 프로세스의 주소 공간에 추가한다.
		 */
		if (!install_page (upage, kpage, writable)) {
			printf ("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* @lock
		 * 다음 페이지로 진행한다.
		 */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* @lock
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

/* @lock
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

	/* @lock
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
/* @lock
 * 여기부터의 코드는 project 3 이후에 사용된다.
 * project 2만을 위한 함수를 구현하려면 위쪽 블록에 구현하라.
 */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* @lock
	 * TODO: 파일에서 세그먼트를 로드하라.
	 */
	/* @lock
	 * TODO: 이 함수는 VA 주소에서 첫 번째 페이지 폴트가 발생했을 때 호출된다.
	 */
	/* @lock
	 * TODO: 이 함수를 호출할 때 VA를 사용할 수 있다.
	 */
}

/* @lock
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
		/* @lock
		 * 이 페이지를 어떻게 채울지 계산한다.
		 * FILE에서 PAGE_READ_BYTES 바이트를 읽고
		 * 마지막 PAGE_ZERO_BYTES 바이트를 0으로 채운다.
		 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* @lock
		 * TODO: lazy_load_segment에 정보를 전달할 aux를 설정하라.
		 */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage, writable,
		                                     lazy_load_segment, aux))
			return false;

		/* @lock
		 * 다음 페이지로 진행한다.
		 */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* @lock
 * USER_STACK에 스택용 PAGE를 만든다. 성공하면 true를 반환한다.
 */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* @lock
	 * TODO: stack_bottom에 스택을 매핑하고 페이지를 즉시 claim하라.
	 * TODO: 성공하면 그에 맞게 rsp를 설정하라.
	 * TODO: 해당 페이지를 스택 페이지로 표시해야 한다.
	 */
	/* @lock
	 * TODO: 여기에 코드를 작성하라.
	 */

	return success;
}
#endif /* VM */
