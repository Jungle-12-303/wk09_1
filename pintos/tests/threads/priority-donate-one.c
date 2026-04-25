/* ============================================================
 * priority-donate-one.c — Priority Donation 의 가장 단순한 시나리오
 *
 * --- Priority Donation 이란 ---
 *
 *   문제 (우선순위 역전):
 *     낮은 우선순위 L 이 락 X 를 잡고 있는 동안,
 *     높은 우선순위 H 가 X 를 기다린다고 하자.
 *     L 보다 우선순위가 높지만 X 는 신경 안 쓰는 M 이 있다면,
 *     L 은 M 에게 밀려 실행되지 못함 → H 도 무한히 대기.
 *     이게 우선순위 역전.
 *
 *   해결 (donation):
 *     H 가 L 에게 자기 우선순위를 "기부" 한다.
 *     L 의 effective priority = max(원래 L, 기부받은 H) = H.
 *     이제 L 이 M 보다 높음 → L 이 실행되어 X 를 빨리 해제 →
 *     H 가 X 를 받아 진행.
 *
 * --- 본 테스트의 시나리오 ---
 *
 *   1. 메인 (PRI_DEFAULT=31) 이 lock 획득.
 *   2. acquire1 (PRI_DEFAULT+1=32) 생성 → preempt 로 실행 →
 *      lock_acquire → 블록 → 메인에게 32 기부.
 *      메인의 effective priority = 32 가 됨.
 *      즉 thread_get_priority() 가 32 를 반환해야 함.
 *   3. 메인 (이제 32) 재개 → "actual priority: 32" 출력.
 *   4. acquire2 (PRI_DEFAULT+2=33) 생성 → preempt 로 실행 →
 *      lock_acquire → 블록 → 메인에게 33 기부.
 *      메인의 effective priority = 33 (max of 32, 33).
 *   5. 메인 (이제 33) 재개 → "actual priority: 33" 출력.
 *   6. 메인이 lock 해제 → 기부 사라짐 → 메인 = 31 로 복귀.
 *      acquire2 (33) 가 lock 받음 → 실행.
 *      acquire2 끝나면 acquire1 (32) → 실행.
 *      → 출력 순서: acquire2 → acquire1.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. lock_acquire 시 holder 에게 자기 priority 기부 (chain 가능).
 *   2. lock_release 시 그 락에 대한 기부만 회수.
 *   3. effective priority = max(original, 모든 기부 priority).
 *   4. 락 큐에서 가장 높은 priority waiter 부터 깨워야 함.
 *
 * --- 구현 힌트 ---
 *
 *   struct thread 에 추가:
 *     int original_priority;
 *     struct list donations;        // 받은 기부들 (list_elem 으로 연결)
 *     struct lock *wait_on_lock;    // 지금 기다리는 락 (chain donation 용)
 *
 *   lock_acquire:
 *     wait_on_lock = lock;
 *     // donate to holder (chain)
 *     while (curr->wait_on_lock)
 *       update_holder_priority (curr->wait_on_lock->holder, curr->priority);
 *     sema_down (...)
 *     wait_on_lock = NULL;
 *
 *   lock_release:
 *     // 이 락에 대한 기부 모두 제거
 *     remove_donations_for_lock (lock);
 *     // priority 재계산
 *     refresh_priority ();
 *     sema_up (...)
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

static thread_func acquire1_thread_func;
static thread_func acquire2_thread_func;

void
test_priority_donate_one (void) 
{
  struct lock lock;

  /* MLFQS 에서는 donation 자체가 사용되지 않음. */
  ASSERT (!thread_mlfqs);

  /* 메인 우선순위가 기본값 (31) 인지 확인. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&lock);
  /* 메인이 락 획득 (낮은 우선순위 holder 역할). */
  lock_acquire (&lock);

  /* acquire1 (32) 생성 → preempt → lock_acquire 에서 블록 → 메인에 32 기부. */
  thread_create ("acquire1", PRI_DEFAULT + 1, acquire1_thread_func, &lock);
  /* 이때 메인 effective priority = 32 여야 PASS. */
  msg ("This thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

  /* acquire2 (33) 생성 → preempt → lock_acquire 에서 블록 → 메인에 33 기부.
     메인 effective priority = max(32, 33) = 33. */
  thread_create ("acquire2", PRI_DEFAULT + 2, acquire2_thread_func, &lock);
  msg ("This thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());

  /* 락 해제 → 메인 priority 31 로 복귀.
     락 큐에서 가장 높은 acquire2 (33) 가 락 획득 → 실행 → 종료.
     이어서 acquire1 (32) → 실행 → 종료.
     → 메시지 순서가 acquire2 먼저, 그 다음 acquire1 이어야 PASS. */
  lock_release (&lock);
  msg ("acquire2, acquire1 must already have finished, in that order.");
  msg ("This should be the last line before finishing this test.");
}

/* acquire1 진입점. lock 획득 시도 → 블록 → 깨어나면 메시지 후 해제. */
static void
acquire1_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire1: got the lock");
  lock_release (lock);
  msg ("acquire1: done");
}

/* acquire2 진입점. acquire1 과 동일 패턴. */
static void
acquire2_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire2: got the lock");
  lock_release (lock);
  msg ("acquire2: done");
}
