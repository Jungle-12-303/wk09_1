/* ============================================================
 * synch.c -- 동기화 프리미티브 (세마포어, 락, 조건변수)
 *
 * Pintos의 동기화(synchronization) 메커니즘을 구현한 파일.
 * Project 1에서 thread.c 다음으로 많이 수정하는 파일이다.
 *
 * 세 가지 동기화 프리미티브:
 *
 * 1. 세마포어 (Semaphore)
 *    - 음이 아닌 정수 카운터 + 대기 큐
 *    - down(P): 값이 0이면 대기, 양수면 1 감소
 *    - up(V): 값을 1 증가, 대기 스레드가 있으면 하나 깨움
 *
 * 2. 락 (Lock)
 *    - 초기값 1인 세마포어의 특수한 형태
 *    - 소유자(holder)가 있다: 같은 스레드만 acquire/release 가능
 *    - Phase 3(priority donation)의 핵심 대상
 *
 * 3. 조건변수 (Condition Variable)
 *    - 특정 조건이 만족될 때까지 스레드를 대기시키는 메커니즘
 *    - 반드시 락과 함께 사용한다
 *    - Mesa 스타일: signal 후 조건을 다시 확인해야 한다
 *
 * Project 1 수정 대상:
 *   Phase 2: sema_down()에서 waiters를 우선순위 순으로 삽입,
 *            sema_up()에서 가장 높은 우선순위 스레드를 깨움,
 *            cond_signal()에서 가장 높은 우선순위 waiter를 깨움
 *   Phase 3: lock_acquire()에서 priority donation 수행,
 *            lock_release()에서 donation 제거 및 우선순위 복원
 *
 * 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생됨.
 * ============================================================ */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* ============================================================
 * 세마포어 (Semaphore)
 *
 * 가장 기본적인 동기화 프리미티브.
 * 내부에 정수 값(value)과 대기 큐(waiters)를 가진다.
 *
 * 핵심 불변식: value는 항상 0 이상이다.
 *
 * 사용 예시:
 *   - value=0으로 초기화하면 "신호 대기"용 (스레드 간 순서 제어)
 *   - value=1로 초기화하면 "상호 배제"용 (= 락과 동일)
 *   - value=N으로 초기화하면 "최대 N개 동시 접근" 제어
 * ============================================================ */

/* ============================================================
 * sema_init -- 세마포어 초기화
 *
 * value를 초기값으로 설정하고, 대기 큐(waiters)를 빈 리스트로 만든다.
 * ============================================================ */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* ============================================================
 * sema_down -- 세마포어 P(감소) 연산
 *
 * value가 양수가 될 때까지 대기한 후, 원자적으로 1 감소시킨다.
 *
 * 동작:
 *   1. 인터럽트를 끈다 (원자성 보장)
 *   2. value == 0이면:
 *      - 현재 스레드를 waiters 리스트에 넣고
 *      - thread_block()으로 잠든다
 *      - 깨어나면 다시 value를 확인한다 (while 루프)
 *   3. value > 0이면 value를 1 감소시킨다
 *   4. 인터럽트를 복원한다
 *
 * while 루프를 쓰는 이유:
 *   sema_up()이 깨워줘도 다른 스레드가 먼저 value를 가져갈 수 있다.
 *   (Mesa 스타일 동기화)
 *
 * 주의:
 *   - 이 함수는 잠들 수 있으므로 인터럽트 핸들러에서 호출 금지
 *   - 인터럽트를 끈 상태에서 호출할 수는 있지만,
 *     잠들면 다음 스레드가 인터럽트를 다시 켠다
 *
 * [Phase 2] list_push_back 대신 우선순위 순으로 삽입해야 한다.
 *           list_insert_ordered(&sema->waiters, &curr->elem, cmp_priority, NULL)
 * ============================================================ */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());    /* 인터럽트 핸들러에서 호출 금지 */

	old_level = intr_disable ();

	/* value가 0이면 대기한다.
	 * 깨어난 후에도 다시 확인해야 하므로 while 사용. */
	while (sema->value == 0) {
		/* [Phase 2] list_push_back -> list_insert_ordered로 변경 필요 */
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();  /* 현재 스레드를 BLOCKED로 만들고 다른 스레드로 전환 */
	}

	sema->value--;  /* 자원 획득 */
	intr_set_level (old_level);
}

/* ============================================================
 * sema_try_down -- 세마포어 비차단 P 연산
 *
 * value가 양수이면 1 감소시키고 true를 반환한다.
 * value가 0이면 대기하지 않고 즉시 false를 반환한다.
 *
 * 잠들지 않으므로 인터럽트 핸들러에서도 호출할 수 있다.
 * ============================================================ */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* ============================================================
 * sema_up -- 세마포어 V(증가) 연산
 *
 * value를 1 증가시키고, 대기 중인 스레드가 있으면 하나를 깨운다.
 *
 * 동작:
 *   1. 인터럽트를 끈다
 *   2. waiters가 비어있지 않으면:
 *      - 맨 앞 스레드를 꺼내서 thread_unblock()으로 READY로 바꾼다
 *   3. value를 1 증가시킨다
 *   4. 인터럽트를 복원한다
 *
 * 인터럽트 핸들러에서도 호출할 수 있다.
 * (thread_unblock()은 잠들지 않으므로 안전)
 *
 * [Phase 2] list_pop_front 대신 가장 높은 우선순위 스레드를 꺼내야 한다.
 *           waiters를 정렬했다면 pop_front로 충분.
 *           정렬 안 했다면 list_max()로 찾아서 제거.
 *           깨운 스레드의 우선순위가 현재보다 높으면 thread_yield() 필요.
 * ============================================================ */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	struct thread *unblocked = NULL;

	ASSERT (sema != NULL);

	old_level = intr_disable ();

	if (!list_empty (&sema->waiters)) {
		struct list_elem *max_elem = list_max (&sema->waiters,
		                                       thread_priority_less, NULL);
		list_remove (max_elem);
		unblocked = list_entry (max_elem, struct thread, elem);
		thread_unblock (unblocked);
	}

	sema->value++;

	if (unblocked != NULL
	    && unblocked->priority > thread_current ()->priority) {
		if (intr_context ())
			intr_yield_on_return ();
		else
			thread_yield ();
	}

	intr_set_level (old_level);
}

/* ============================================================
 * sema_self_test -- 세마포어 자체 테스트
 *
 * 두 스레드 사이에서 세마포어로 핑퐁하는 테스트.
 * main 스레드와 sema-test 스레드가 번갈아가며
 * sema[0]과 sema[1]을 up/down하여 10번 교대 실행한다.
 *
 * 동작 순서:
 *   main: sema_up(sema[0]) -> sema_down(sema[1]) (대기)
 *   test: sema_down(sema[0]) -> sema_up(sema[1]) (main 깨움)
 *   ... 10번 반복 ...
 * ============================================================ */
static void sema_test_helper (void *sema_);

void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);     /* test 스레드를 깨운다 */
		sema_down (&sema[1]);   /* test 스레드가 깨워줄 때까지 대기 */
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 보조 스레드 함수. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);   /* main이 깨워줄 때까지 대기 */
		sema_up (&sema[1]);     /* main을 깨운다 */
	}
}

/* ============================================================
 * 락 (Lock)
 *
 * 초기값 1인 세마포어의 특수한 형태.
 * 세마포어와의 차이점 두 가지:
 *
 * 1. 세마포어는 값이 1보다 클 수 있지만,
 *    락은 한 번에 하나의 스레드만 소유할 수 있다.
 *
 * 2. 세마포어는 소유자가 없다 (한 스레드가 down하고 다른 스레드가 up 가능).
 *    락은 소유자(holder)가 있다 (같은 스레드가 acquire하고 release해야 한다).
 *
 * Pintos의 락은 재귀적(recursive)이지 않다.
 * 이미 보유한 락을 다시 acquire하면 ASSERT 실패.
 *
 * Phase 3(Priority Donation)에서 핵심이 되는 구조체:
 *   - lock->holder: 현재 락을 보유한 스레드
 *   - 스레드가 락 획득 대기 중이면 holder에게 우선순위를 기부한다
 * ============================================================ */

/* ============================================================
 * lock_init -- 락 초기화
 *
 * holder를 NULL로 설정하고, 내부 세마포어를 1로 초기화한다.
 * value=1이므로 첫 번째 sema_down()은 즉시 성공한다.
 * ============================================================ */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* ============================================================
 * lock_acquire -- 락 획득 (필요하면 대기)
 *
 * 락이 사용 가능하면 즉시 획득하고, holder를 현재 스레드로 설정한다.
 * 다른 스레드가 보유 중이면 해제될 때까지 대기한다(sema_down).
 *
 * 주의:
 *   - 이미 보유한 락을 다시 획득하면 ASSERT 실패 (비재귀적)
 *   - 잠들 수 있으므로 인터럽트 핸들러에서 호출 금지
 *
 * [Phase 3] Priority Donation 구현 위치:
 *   sema_down() 호출 전에 다음 로직을 추가해야 한다:
 *
 *   1. 현재 스레드의 wait_on_lock을 이 lock으로 설정
 *   2. lock->holder가 있고, holder의 priority가 현재보다 낮으면:
 *      - holder의 priority를 현재 스레드의 priority로 올린다
 *      - holder도 다른 lock을 기다리고 있으면 연쇄 기부 (nested donation)
 *   3. sema_down() 이후 (락 획득 성공):
 *      - wait_on_lock을 NULL로 설정
 *      - holder를 현재 스레드로 설정
 *      - 현재 스레드의 donations 리스트에 등록
 * ============================================================ */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());                      /* 인터럽트 핸들러에서 호출 금지 */
	ASSERT (!lock_held_by_current_thread (lock));    /* 이미 보유한 락 재획득 금지 */

	/* [Phase 3] 여기에 donation 로직 추가 */

	sema_down (&lock->semaphore);   /* 락이 풀릴 때까지 대기 */
	lock->holder = thread_current ();  /* 소유자를 현재 스레드로 설정 */
}

/* ============================================================
 * lock_try_acquire -- 락 비차단 획득 시도
 *
 * 락이 사용 가능하면 획득하고 true를 반환한다.
 * 다른 스레드가 보유 중이면 대기하지 않고 즉시 false를 반환한다.
 *
 * 잠들지 않으므로 인터럽트 핸들러에서도 호출할 수 있다.
 * ============================================================ */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* ============================================================
 * lock_release -- 락 해제
 *
 * 현재 스레드가 보유한 락을 해제한다.
 * holder를 NULL로 설정하고, sema_up()으로 대기 스레드를 깨운다.
 *
 * 현재 스레드가 소유자가 아니면 ASSERT 실패.
 * 인터럽트 핸들러에서는 락을 획득할 수 없으므로 해제도 의미 없다.
 *
 * [Phase 3] Priority Donation 제거 위치:
 *   holder를 NULL로 설정하기 전에 다음 로직을 추가해야 한다:
 *
 *   1. 현재 스레드의 donations 리스트에서 이 lock을 기다리는
 *      스레드들을 모두 제거한다
 *   2. donations 리스트에 남은 스레드 중 가장 높은 priority와
 *      original_priority 중 큰 값으로 현재 스레드의 priority를 복원한다
 *   3. 필요하면 thread_yield()로 선점
 * ============================================================ */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	/* [Phase 3] 여기에 donation 제거 + 우선순위 복원 로직 추가 */

	lock->holder = NULL;
	sema_up (&lock->semaphore);   /* 대기 스레드를 깨운다 */
}

/* ============================================================
 * lock_held_by_current_thread -- 현재 스레드가 이 락을 보유 중인지 확인
 *
 * 보유 중이면 true, 아니면 false를 반환한다.
 *
 * 주의: "다른 스레드가 이 락을 보유 중인지"를 검사하는 것은
 * 경쟁 조건(race condition) 때문에 신뢰할 수 없다.
 * 확인하는 순간 상태가 바뀔 수 있기 때문이다.
 * ============================================================ */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* ============================================================
 * 조건변수 (Condition Variable)
 *
 * 특정 조건이 만족될 때까지 스레드를 대기시키는 고수준 동기화 도구.
 * 반드시 락(Lock)과 함께 사용한다.
 *
 * 기본 사용 패턴:
 *
 *   lock_acquire(&lock);
 *   while (!조건)           // 반드시 while (Mesa 스타일)
 *       cond_wait(&cond, &lock);
 *   ... 조건이 참일 때의 작업 ...
 *   lock_release(&lock);
 *
 * 조건을 바꾸는 쪽:
 *
 *   lock_acquire(&lock);
 *   조건 = true;
 *   cond_signal(&cond, &lock);   // 대기자 하나 깨움
 *   lock_release(&lock);
 *
 * 구현 원리:
 *   각 대기 스레드마다 전용 세마포어(semaphore_elem)를 만들어서
 *   cond->waiters 리스트에 넣는다.
 *   signal 시 세마포어 하나를 up하여 해당 스레드만 깨운다.
 *
 * Mesa vs Hoare 스타일:
 *   Mesa: signal 후 signaler가 계속 실행. waiter는 깨어나도
 *         조건을 다시 확인해야 한다 (while 필수).
 *   Hoare: signal 후 waiter가 즉시 실행. 조건이 보장됨.
 *   Pintos는 Mesa 스타일이다.
 * ============================================================ */

/* 리스트에 들어가는 세마포어 래퍼.
 * 각 대기 스레드마다 하나씩 스택에 생성된다.
 * cond->waiters 리스트의 원소로 사용된다. */
struct semaphore_elem {
	struct list_elem elem;              /* cond->waiters 리스트 연결용 */
	struct semaphore semaphore;         /* 이 스레드 전용 세마포어 (초기값 0) */
};

/* ============================================================
 * cond_init -- 조건변수 초기화
 *
 * 대기 큐(waiters)를 빈 리스트로 만든다.
 * ============================================================ */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* ============================================================
 * cond_wait -- 조건변수에서 대기
 *
 * 락을 해제하고, 신호(signal)가 올 때까지 잠든다.
 * 깨어나면 락을 다시 획득한 후 리턴한다.
 *
 * 동작 순서:
 *   1. 스택에 semaphore_elem을 만들고, 세마포어를 0으로 초기화
 *   2. semaphore_elem을 cond->waiters에 추가
 *   3. lock을 해제 (다른 스레드가 조건을 바꿀 수 있게)
 *   4. sema_down()으로 대기 (signal이 sema_up()을 호출할 때까지)
 *   5. 깨어나면 lock을 다시 획득
 *
 * 전제 조건:
 *   - cond_wait() 호출 전에 반드시 lock을 보유하고 있어야 한다
 *   - 인터럽트 핸들러에서 호출 금지 (잠들 수 있으므로)
 * ============================================================ */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);    /* 전용 세마포어 생성 (0 = 아직 신호 없음) */
	list_push_back (&cond->waiters, &waiter.elem);  /* 대기 큐에 등록 */
	lock_release (lock);                  /* 락 해제 */
	sema_down (&waiter.semaphore);        /* 신호가 올 때까지 대기 */
	lock_acquire (lock);                  /* 깨어나면 락 재획득 */
}

/* ============================================================
 * cond_signal -- 조건변수에서 대기 중인 스레드 하나를 깨움
 *
 * waiters 리스트에서 맨 앞의 semaphore_elem을 꺼내서
 * sema_up()으로 해당 스레드를 깨운다.
 *
 * 대기 스레드가 없으면 아무 일도 하지 않는다 (신호 유실).
 *
 * 전제 조건:
 *   - 이 함수 호출 전에 lock을 보유하고 있어야 한다
 *   - 인터럽트 핸들러에서 호출 금지
 *
 * [Phase 2] list_pop_front 대신 가장 높은 우선순위 스레드의
 *           세마포어를 찾아서 up해야 한다.
 *           방법: waiters의 각 semaphore_elem 안의 semaphore.waiters에서
 *                 스레드를 꺼내 우선순위를 비교한다.
 * ============================================================ */
static bool
sema_priority_less (const struct list_elem *lhs,
                    const struct list_elem *rhs,
                    void *aux UNUSED) {
	const struct semaphore_elem *lhs_sema =
		list_entry (lhs, struct semaphore_elem, elem);
	const struct semaphore_elem *rhs_sema =
		list_entry (rhs, struct semaphore_elem, elem);
	const struct thread *lhs_thread = list_entry (
		list_front (&lhs_sema->semaphore.waiters), struct thread, elem);
	const struct thread *rhs_thread = list_entry (
		list_front (&rhs_sema->semaphore.waiters), struct thread, elem);
	return lhs_thread->priority < rhs_thread->priority;
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		struct list_elem *max_elem = list_max (&cond->waiters,
		                                       sema_priority_less, NULL);
		list_remove (max_elem);
		sema_up (&list_entry (max_elem, struct semaphore_elem, elem)->semaphore);
	}
}

/* ============================================================
 * cond_broadcast -- 조건변수에서 대기 중인 모든 스레드를 깨움
 *
 * waiters의 모든 세마포어에 sema_up()을 호출한다.
 * cond_signal()을 waiters가 빌 때까지 반복 호출하는 것과 같다.
 *
 * 전제 조건:
 *   - lock을 보유하고 있어야 한다
 * ============================================================ */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
