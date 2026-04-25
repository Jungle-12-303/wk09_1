/* ============================================================
 * priority-donate-multiple2.c — 다중 기부 + 락 해제 순서 변경
 *
 * --- priority-donate-multiple 과의 차이 ---
 *
 *   priority-donate-multiple : 마지막 기부 (b, 33) 부터 회수.
 *   priority-donate-multiple2: 더 일찍 받은 기부 (a, 34) 부터 회수.
 *
 *   해제 순서가 달라도 effective priority 가 올바르게 계산되는지 검증.
 *
 * --- 시나리오 ---
 *
 *   1. 메인 (31) 이 락 a, b 모두 획득.
 *   2. thread a (PRI_DEFAULT+3=34) 생성 → lock_a 대기 → 메인에 34 기부.
 *      메인 effective = 34.
 *   3. thread c (PRI_DEFAULT+1=32) 생성 → 락과 무관, 그냥 ready 큐 대기.
 *      메인 (34) 이 c (32) 보다 높아 메인이 계속 실행.
 *   4. thread b (PRI_DEFAULT+5=36) 생성 → lock_b 대기 → 메인에 36 기부.
 *      메인 effective = max(34, 36) = 36.
 *   5. 메인이 lock_a 해제 → a 의 기부만 회수 → 메인 effective = 36
 *      (b 의 기부는 그대로). thread a 가 락 받고 실행 → 종료.
 *   6. 메인이 lock_b 해제 → b 의 기부 회수 → 메인 = 31 (원래 값).
 *      thread b 가 락 받고 실행 → 종료.
 *      그 다음 thread c (32) 가 메인 (31) 보다 높아 실행 → 종료.
 *
 *   → 출력 순서: b, a, c (우선순위 순으로 정리되며 종료).
 *
 * --- 핵심 검증 포인트 ---
 *
 *   priority-donate-multiple 과 동일하지만, 해제 순서가 다를 때도
 *   기부 list 에서 "어느 락의 기부인가" 를 정확히 추적해야 한다.
 *   단순 LIFO 나 FIFO 가 아니라 lock 단위 식별이 필요.
 *
 * --- 출처 ---
 *
 *   Godmar Back. Stanford CS 140 (1999) 기반.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func a_thread_func;
static thread_func b_thread_func;
static thread_func c_thread_func;

void
test_priority_donate_multiple2 (void) 
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

  /* thread a (34) 생성 → lock_a 대기 → 메인에 34 기부. */
  thread_create ("a", PRI_DEFAULT + 3, a_thread_func, &a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 3, thread_get_priority ());

  /* thread c (32) 생성 → 락과 무관 (NULL 인자) → 그냥 ready 큐 대기.
     메인 (34) 이 c (32) 보다 높아 c 는 아직 실행 안 됨. */
  thread_create ("c", PRI_DEFAULT + 1, c_thread_func, NULL);

  /* thread b (36) 생성 → lock_b 대기 → 메인에 36 기부.
     메인 effective = max(34, 36) = 36. */
  thread_create ("b", PRI_DEFAULT + 5, b_thread_func, &b);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 5, thread_get_priority ());

  /* lock_a 해제 — a 의 기부 (34) 만 회수.
     b 의 기부 (36) 는 유지 → 메인 effective = 36. */
  lock_release (&a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 5, thread_get_priority ());

  /* lock_b 해제 → b 의 기부 회수 → 메인 = 31 (원래).
     thread b (36) → thread a (34) → thread c (32) 순으로 실행. */
  lock_release (&b);
  msg ("Threads b, a, c should have just finished, in that order.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
}

/* thread a 진입점. */
static void
a_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread a acquired lock a.");
  lock_release (lock);
  msg ("Thread a finished.");
}

/* thread b 진입점. */
static void
b_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread b acquired lock b.");
  lock_release (lock);
  msg ("Thread b finished.");
}

/* thread c 진입점. 락과 무관, 그냥 메시지만 찍고 종료. */
static void
c_thread_func (void *a_ UNUSED) 
{
  msg ("Thread c finished.");
}
