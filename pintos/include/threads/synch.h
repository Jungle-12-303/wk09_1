#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 카운팅 세마포어.
 *   value > 0이면 자원 사용 가능, sema_down()이 바로 통과.
 *   value == 0이면 자원 없음, sema_down() 호출 시 waiters에서 대기.
 *   sema_up()이 value를 올리고 대기 스레드 하나를 깨운다. */
struct semaphore {
	unsigned value;             /* 현재 값. 0이면 대기, 양수이면 통과. */
	struct list waiters;        /* 대기 중인 스레드 리스트. */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* 락 (Lock).
 *   세마포어를 value=1로 감싼 것. 한 번에 하나의 스레드만 보유 가능.
 *   holder 필드로 현재 보유자를 추적한다.
 *   Priority Donation(Phase 3)에서 holder를 통해 기부 대상을 찾는다. */
struct lock {
	struct thread *holder;      /* 락을 보유한 스레드 (NULL이면 미보유). */
	struct semaphore semaphore; /* 접근 제어용 이진 세마포어 (value 0 또는 1). */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* 조건 변수 (Condition Variable).
 *   락을 보유한 상태에서 특정 조건을 기다릴 때 사용.
 *   cond_wait()는 락을 풀고 대기, 깨어나면 락을 다시 획득.
 *   cond_signal()은 대기자 하나를 깨우고, cond_broadcast()는 전부 깨운다. */
struct condition {
	struct list waiters;        /* 대기 중인 세마포어 리스트 (semaphore_elem). */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* 최적화 배리어.
 *
 * 컴파일러가 이 배리어 전후로 연산 순서를 바꾸지 못하게 한다.
 * 레퍼런스 가이드의 "Optimization Barriers" 참고. */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
