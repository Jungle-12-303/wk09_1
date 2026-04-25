/* ============================================================
 * priority-donate-multiple.c — 두 락에서 받은 다중 기부 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   메인이 두 락 A, B 를 모두 획득한 상태에서 더 높은 우선순위 두 스레드
 *   가 각각 락 A, B 를 기다리며 기부할 때, 메인의 effective priority 가
 *   "두 기부 중 더 높은 값" 이 되어야 한다.
 *   락 해제 시 해당 락의 기부만 회수되어 priority 가 점진적으로 복귀.
 *
 * --- 시나리오 ---
 *
 *   1. 메인 (PRI_DEFAULT=31) 이 lock_a, lock_b 모두 획득.
 *   2. thread a (32) 생성 → lock_a 대기 → 메인에 32 기부.
 *      메인 effective priority = 32.
 *   3. thread b (33) 생성 → lock_b 대기 → 메인에 33 기부.
 *      메인 effective priority = max(32, 33) = 33.
 *   4. 메인이 lock_b 해제 → b 의 기부 회수 → 메인 priority = 32.
 *      thread b 가 락 받고 종료.
 *   5. 메인이 lock_a 해제 → a 의 기부 회수 → 메인 priority = 31 (원래 값).
 *      thread a 가 락 받고 종료.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. 한 스레드가 여러 기부를 동시에 받을 수 있어야 함.
 *   2. effective priority = max(original, 모든 기부).
 *   3. 락 해제 시 그 락에 대한 기부만 회수, 나머지는 유지.
 *
 * --- 구현 힌트 ---
 *
 *   기부 정보는 (donor thread, donated priority, lock) 의 튜플로 저장.
 *   기부 list 를 lock 단위로 정리하면 release 시 해당 lock 의 기부만
 *   골라 제거 가능.
 *
 * --- 출처 ---
 *
 *   Stanford CS 140 (1999). arens 가 수정.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func a_thread_func;
static thread_func b_thread_func;

void
test_priority_donate_multiple (void) 
{
  struct lock a, b;

  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  /* 메인 우선순위가 기본값인지 확인. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&a);
  lock_init (&b);

  /* 메인이 두 락 모두 획득. */
  lock_acquire (&a);
  lock_acquire (&b);

  /* thread a (32) 생성 → lock_a 에서 블록 → 메인에 32 기부. */
  thread_create ("a", PRI_DEFAULT + 1, a_thread_func, &a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

  /* thread b (33) 생성 → lock_b 에서 블록 → 메인에 33 기부.
     메인 effective priority = max(32, 33) = 33. */
  thread_create ("b", PRI_DEFAULT + 2, b_thread_func, &b);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());

  /* lock_b 해제 → b 의 기부 회수 → 메인 priority = 32 (a 의 기부만 남음).
     thread b 가 락 받고 실행 → 종료. */
  lock_release (&b);
  msg ("Thread b should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

  /* lock_a 해제 → a 의 기부 회수 → 메인 priority = 31 (원래 값).
     thread a 가 락 받고 실행 → 종료. */
  lock_release (&a);
  msg ("Thread a should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
}

/* thread a 진입점. lock_a 획득 시도 → 블록 → 깨어나면 메시지. */
static void
a_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread a acquired lock a.");
  lock_release (lock);
  msg ("Thread a finished.");
}

/* thread b 진입점. lock_b 획득 시도 → 블록 → 깨어나면 메시지. */
static void
b_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread b acquired lock b.");
  lock_release (lock);
  msg ("Thread b finished.");
}
