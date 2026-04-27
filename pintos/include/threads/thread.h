#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* @lock 스레드 생명 주기의 상태. */
enum thread_status {
	THREAD_RUNNING,     /* @lock 실행 중인 스레드. */
	THREAD_READY,       /* @lock 실행 중은 아니지만 실행할 준비가 된 스레드. */
	THREAD_BLOCKED,     /* @lock 이벤트가 발생하기를 기다리는 스레드. */
	THREAD_DYING        /* @lock 곧 제거될 스레드. */
};

/* @lock 스레드 식별자 타입.
   원하는 어떤 타입으로든 다시 정의할 수 있다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* @lock tid_t의 오류 값. */

/* @lock 스레드 우선순위. */
#define PRI_MIN 0                       /* @lock 가장 낮은 우선순위. */
#define PRI_DEFAULT 31                  /* @lock 기본 우선순위. */
#define PRI_MAX 63                      /* @lock 가장 높은 우선순위. */

/* @lock 커널 스레드 또는 사용자 프로세스.
 *
 * 각 스레드 구조체는 자신만의 4 kB 페이지에 저장된다. 스레드 구조체
 * 자체는 페이지의 맨 아래(offset 0)에 놓인다. 페이지의 나머지 공간은
 * 스레드의 커널 스택으로 예약되며, 이 스택은 페이지의 맨 위(offset 4 kB)
 * 에서 아래쪽으로 자란다. 그림으로 나타내면 다음과 같다.
 *
 *      4 kB +---------------------------------+
 *           |          커널 스택              |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         아래쪽으로 성장          |
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
 * 여기서 얻는 결론은 두 가지다.
 *
 *    1. 첫째, `struct thread'가 너무 커지면 안 된다. 너무 커지면
 *       커널 스택을 위한 공간이 충분하지 않게 된다. 기본 `struct thread'는
 *       크기가 몇 바이트에 불과하다. 아마 1 kB보다 훨씬 작게 유지되어야 한다.
 *
 *    2. 둘째, 커널 스택이 너무 커지면 안 된다. 스택이 오버플로우하면
 *       스레드 상태를 손상시킨다. 따라서 커널 함수는 큰 구조체나 배열을
 *       비정적 지역 변수로 할당하면 안 된다. 대신 malloc()이나
 *       palloc_get_page()를 사용한 동적 할당을 사용한다.
 *
 * 이 두 문제 중 하나가 발생했을 때의 첫 증상은 아마 thread_current()의
 * assertion 실패일 것이다. thread_current()는 실행 중인 스레드의
 * `struct thread' 안에 있는 `magic' 멤버가 THREAD_MAGIC으로 설정되어
 * 있는지 검사한다. 스택 오버플로우는 보통 이 값을 바꾸며 assertion을
 * 발생시킨다. */
/* @lock `elem' 멤버는 두 가지 목적을 가진다. 실행 큐(thread.c)의 원소가
 * 될 수도 있고, 세마포어 대기 리스트(synch.c)의 원소가 될 수도 있다.
 * 이 두 사용 방식은 서로 배타적이기 때문에 가능하다. ready 상태인
 * 스레드만 실행 큐에 있고, blocked 상태인 스레드만 세마포어 대기
 * 리스트에 있다. */
struct thread {

	/* thread.c 소유. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름 (디버깅용). */
	int64_t wakeup_tick;                /* sleep 깨어날 절대 tick. 0 = 자고 있지 않음. */
	int priority;                       /* 우선순위 (0~63). */

	/* @lock thread.c와 synch.c가 공유한다. */
	struct list_elem elem;      //이전 노드와 다음 노드를 가리킨다        /* @lock 리스트 원소. */

#ifdef USERPROG
	/* @lock userprog/process.c 소유. */
	uint64_t *pml4;                     /* @lock 페이지 맵 레벨 4. */
#endif
#ifdef VM
	/* @lock 스레드가 소유한 전체 가상 메모리에 대한 테이블. */
	struct supplemental_page_table spt;
#endif

	/* @lock thread.c 소유. */
	struct intr_frame tf;               /* @lock 전환에 필요한 정보. */
	unsigned magic;                     /* @lock 스택 오버플로우를 감지한다. */
};

/* @lock false이면 기본값으로 라운드 로빈 스케줄러를 사용한다.
   true이면 다단계 피드백 큐 스케줄러를 사용한다.
   커널 명령줄 옵션 "-o mlfqs"로 제어된다. */
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

#endif /* @lock threads/thread.h */
