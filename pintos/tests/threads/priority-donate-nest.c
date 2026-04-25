/* ============================================================
 * priority-donate-nest.c — Nested (Chain) Priority Donation 검증
 *
 * --- Nested Donation 이란 ---
 *
 *   H 가 락 X 를 기다림 → X 의 holder M 이 또 다른 락 Y 를 기다림 →
 *   Y 의 holder L. 이때 H 의 priority 가 M 을 거쳐 L 까지 전달되어야 함.
 *
 *   단순한 1 단계 donation 으로는 부족 — chain 을 따라 끝까지 전파해야
 *   L 이 빨리 실행되어 Y 를 풀고, 그래야 M 이 X 를 풀고, H 가 진행.
 *
 * --- 시나리오 ---
 *
 *   1. 메인 L (31) 이 lock_a 획득.
 *   2. thread M (32) 생성. M 의 동작:
 *      a. lock_b 획득.
 *      b. lock_a 획득 시도 → L 이 잡고 있어 블록 → L 에 32 기부.
 *      L 의 effective priority = 32.
 *   3. thread H (33) 생성. H 의 동작: lock_b 획득 시도 → M 이 잡고 있어
 *      블록 → M 에 33 기부. **M 이 받은 기부가 L 까지 전파** 되어야 함.
 *      L 의 effective priority = 33.
 *   4. 메인 L 이 lock_a 해제 → M 의 기부 회수 → L = 31 → M 이 lock_a 획득
 *      → M 이 lock_a 해제 → M 이 lock_b 해제 → H 가 lock_b 획득 → 종료
 *      → M 종료 → L 종료.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   lock_acquire 시 holder 가 또 다른 락에서 기다리고 있으면, 그 holder 에게
 *   recursive 하게 기부해야 한다 (chain donation).
 *
 *   while (curr->wait_on_lock != NULL) {
 *     update_priority (curr->wait_on_lock->holder, donor_priority);
 *     curr = curr->wait_on_lock->holder;
 *   }
 *
 *   재귀 깊이 제한 두는 게 안전 (예: depth ≤ 8).
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

/* M 에게 두 락을 한 번에 전달하기 위한 보조 구조체. */
struct locks 
  {
    struct lock *a;
    struct lock *b;
  };

static thread_func medium_thread_func;
static thread_func high_thread_func;

void
test_priority_donate_nest (void) 
{
  struct lock a, b;
  struct locks locks;

  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  /* 메인 우선순위가 기본값인지 확인. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&a);
  lock_init (&b);

  /* L (= 메인) 이 lock_a 획득. */
  lock_acquire (&a);

  /* M (32) 생성. M 은 lock_b → lock_a 순서로 잡으려 함. */
  locks.a = &a;
  locks.b = &b;
  thread_create ("medium", PRI_DEFAULT + 1, medium_thread_func, &locks);
  thread_yield ();
  /* 이 시점: M 이 lock_b 잡고 lock_a 에서 블록 → L 에 32 기부.
     L effective = 32. */
  msg ("Low thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

  /* H (33) 생성. H 는 lock_b 잡으려 함. */
  thread_create ("high", PRI_DEFAULT + 2, high_thread_func, &b);
  thread_yield ();
  /* 이 시점: H 가 lock_b 에서 블록 → M 에 33 기부 → chain 으로 L 도 33.
     L effective = 33. */
  msg ("Low thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());

  /* L 이 lock_a 해제 → M 의 기부 회수 (L = 31).
     이제 M 이 lock_a 받고 진행 → 메시지 → 락 해제 → H 가 lock_b 받음. */
  lock_release (&a);
  thread_yield ();
  msg ("Medium thread should just have finished.");
  msg ("Low thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
}

/* M 진입점. lock_b → lock_a 순으로 획득 시도. */
static void
medium_thread_func (void *locks_) 
{
  struct locks *locks = locks_;

  lock_acquire (locks->b);
  /* 여기서 lock_a 잡으려고 시도 → L 에 기부 → 블록. */
  lock_acquire (locks->a);

  /* 깨어남 = L 이 lock_a 풀어줌. 이때 H 의 기부가 chain 으로 M 에도 와있음. */
  msg ("Medium thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());
  msg ("Medium thread got the lock.");

  lock_release (locks->a);
  thread_yield ();

  lock_release (locks->b);
  thread_yield ();

  msg ("High thread should have just finished.");
  msg ("Middle thread finished.");
}

/* H 진입점. lock_b 획득 시도 → M 에 기부 → chain 으로 L 까지 전파. */
static void
high_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("High thread got the lock.");
  lock_release (lock);
  msg ("High thread finished.");
}
