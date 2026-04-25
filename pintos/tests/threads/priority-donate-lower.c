/* ============================================================
 * priority-donate-lower.c — 기부 받은 동안 priority 하향 시도 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   donation 받은 상태에서 thread_set_priority() 로 자기 priority 를
 *   낮추면, **donation 이 끝날 때까지** 효과가 나타나면 안 된다.
 *
 *   즉 effective priority = max(현재 base, 받은 모든 donation).
 *   base 만 낮춰도 donation 이 살아있는 동안엔 effective 는 그대로 높음.
 *
 * --- 시나리오 ---
 *
 *   1. 메인 (PRI_DEFAULT=31) 이 lock 획득.
 *   2. acquire (PRI_DEFAULT+10=41) 생성 → lock_acquire 블록 → 메인에 41 기부.
 *      메인 effective = 41.
 *   3. 메인이 자기 base priority 를 PRI_DEFAULT-10=21 로 낮춤.
 *      그러나 donation 이 살아있으므로 effective = max(21, 41) = 41 유지.
 *   4. 메인이 lock 해제 → donation 회수 → effective = base = 21.
 *      acquire 가 락 받음 → 종료.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   thread_set_priority 가 base priority 만 바꾸고 effective 는 따로
 *   관리해야 한다. 단순히 priority 필드 하나만 쓰는 구현은 이 테스트에서
 *   실패.
 *
 * --- 구현 힌트 ---
 *
 *   struct thread {
 *     int priority;             // effective (= 실제 스케줄링에 쓰는 값)
 *     int original_priority;    // base (= 사용자가 set 한 값)
 *     struct list donations;    // 받은 기부들
 *   };
 *
 *   void thread_set_priority (int new_priority) {
 *     thread_current ()->original_priority = new_priority;
 *     refresh_priority ();        // donation 보고 effective 재계산
 *     yield_if_lower_than_ready_max ();
 *   }
 *
 *   void refresh_priority (void) {
 *     struct thread *t = thread_current ();
 *     t->priority = t->original_priority;
 *     for each donation d in t->donations:
 *       if (d.priority > t->priority)
 *         t->priority = d.priority;
 *   }
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func acquire_thread_func;

void
test_priority_donate_lower (void) 
{
  struct lock lock;

  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  /* 메인 우선순위가 기본값인지 확인. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&lock);
  /* 메인이 락 획득. */
  lock_acquire (&lock);
  /* acquire (PRI_DEFAULT+10) 생성 → 즉시 lock_acquire 시도 → 블록 →
     메인에 PRI_DEFAULT+10 기부 → 메인 effective = PRI_DEFAULT+10. */
  thread_create ("acquire", PRI_DEFAULT + 10, acquire_thread_func, &lock);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 10, thread_get_priority ());

  /* 메인이 base priority 를 PRI_DEFAULT-10 으로 낮춤.
     하지만 donation 이 살아있으므로 effective = max(base, donation) = +10
     이 유지되어야 한다. */
  msg ("Lowering base priority...");
  thread_set_priority (PRI_DEFAULT - 10);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 10, thread_get_priority ());

  /* 락 해제 → donation 회수 → effective = base = -10. */
  lock_release (&lock);
  msg ("acquire must already have finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT - 10, thread_get_priority ());
}

/* acquire 진입점. lock 획득 시도 → 블록 → 깨어나면 메시지. */
static void
acquire_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("acquire: got the lock");
  lock_release (lock);
  msg ("acquire: done");
}
