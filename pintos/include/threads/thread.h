#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* 스레드 생명주기 상태.
 * 스레드는 항상 아래 네 상태 중 하나에 있다. */
enum thread_status {
	THREAD_RUNNING,     /* 현재 CPU에서 실행 중. */
	THREAD_READY,       /* 실행 준비 완료, ready_list에서 대기 중. */
	THREAD_BLOCKED,     /* 이벤트(세마포어 등) 대기 중. sema_up 등으로 깨워야 함. */
	THREAD_DYING        /* 곧 파괴될 예정. schedule()에서 destruction_req에 등록됨. */
};

/* 스레드 식별자 타입. 필요하면 다른 타입으로 재정의 가능. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* tid_t 에러 값. */

/* 스레드 우선순위 범위.
 *   PRI_MIN = 0  (최저), PRI_DEFAULT = 31 (기본), PRI_MAX = 63 (최고)
 *   숫자가 클수록 높은 우선순위. */
#define PRI_MIN 0                       /* 최저 우선순위. */
#define PRI_DEFAULT 31                  /* 기본 우선순위. */
#define PRI_MAX 63                      /* 최고 우선순위. */

/* 커널 스레드 또는 유저 프로세스.
 *
 * 각 스레드 구조체는 자체 4KB 페이지에 저장된다.
 * 구조체 자체는 페이지 맨 아래(오프셋 0)에 위치하고,
 * 나머지 공간은 커널 스택으로 사용되며 페이지 꼭대기(오프셋 4KB)에서
 * 아래 방향으로 자란다. 그림으로 보면:
 *
 *      4 kB +---------------------------------+
 *           |          커널 스택              |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         아래로 성장              |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * 이 구조에서 주의할 점 두 가지:
 *
 *    1. struct thread가 너무 커지면 안 된다.
 *       커지면 커널 스택 공간이 부족해진다.
 *       기본 struct thread는 수십 바이트 수준이며,
 *       1KB 미만으로 유지하는 것이 좋다.
 *
 *    2. 커널 스택이 너무 커지면 안 된다.
 *       스택 오버플로가 발생하면 스레드 상태를 훼손한다.
 *       따라서 커널 함수에서 큰 구조체나 배열을
 *       지역 변수로 선언하면 안 된다.
 *       대신 malloc()이나 palloc_get_page()로
 *       동적 할당을 사용한다.
 *
 * 두 문제 모두 첫 증상은 thread_current()의 ASSERT 실패다.
 * thread_current()는 실행 중인 스레드의 magic 멤버가
 * THREAD_MAGIC으로 설정되어 있는지 검사하는데,
 * 스택 오버플로가 발생하면 이 값이 바뀌어 ASSERT가 터진다. */

/* elem 멤버는 이중 용도다.
 * ready_list의 원소(thread.c)이거나,
 * 세마포어 대기 리스트의 원소(synch.c)일 수 있다.
 * 이 두 용도는 상호 배타적이기 때문에 가능하다:
 * READY 상태 스레드만 ready_list에, BLOCKED 상태 스레드만
 * 세마포어 대기 리스트에 들어간다. */
struct thread {
	/* thread.c 소유. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름 (디버깅용). */
	int64_t wakeup_tick;                /* sleep 깨어날 절대 tick. 0 = 자고 있지 않음. */
	int priority;                       /* 우선순위 (0~63). */

	/* thread.c와 synch.c가 공유. */
	struct list_elem elem;              /* 리스트 원소 (ready_list 또는 waiters). */

#ifdef USERPROG
	/* userprog/process.c 소유. */
	uint64_t *pml4;                     /* 4단계 페이지 맵 (유저 프로세스용). */
#endif
#ifdef VM
	/* 이 스레드가 소유한 전체 가상 메모리 테이블. */
	struct supplemental_page_table spt;
#endif

	/* thread.c 소유. */
	struct intr_frame tf;               /* 컨텍스트 스위칭 정보. */
	unsigned magic;                     /* 스택 오버플로 감지용 매직 넘버 (THREAD_MAGIC). */
};

/* false(기본값)이면 라운드 로빈 스케줄러 사용.
 * true이면 다단계 피드백 큐(MLFQS) 스케줄러 사용.
 * 커널 커맨드라인 옵션 "-o mlfqs"로 제어. */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

void thread_sleep (int64_t wakeup_tick);
void thread_awake (int64_t current_tick);

bool thread_priority_less (const struct list_elem *lhs,
                           const struct list_elem *rhs,
                           void *aux);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
