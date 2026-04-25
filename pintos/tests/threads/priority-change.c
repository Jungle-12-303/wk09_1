/* ============================================================
 * priority-change.c — 자기 우선순위 하향 시 즉시 양보 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   현재 실행 중인 스레드가 thread_set_priority() 로 자기 우선순위를
 *   낮추어 더 이상 시스템에서 가장 높은 우선순위가 아니게 되면,
 *   즉시 CPU 를 양보해야 한다.
 *
 *   양보 대상은 ready 큐의 더 높은 우선순위 스레드.
 *
 * --- 시나리오 ---
 *
 *   1. 메인 (PRI_DEFAULT=31) 이 실행 중.
 *   2. thread 2 (PRI_DEFAULT+1=32) 생성 → preempt 로 thread 2 가 즉시 실행.
 *      thread 2 가 자기 우선순위를 PRI_DEFAULT-1=30 으로 낮춤.
 *      이 순간 thread 2 < 메인이라 thread 2 가 즉시 양보 → 메인 재개.
 *   3. 메인이 자기 우선순위를 PRI_DEFAULT-2=29 로 낮춤.
 *      이 순간 메인 < thread 2 (=30) 이라 메인이 즉시 양보 → thread 2 재개.
 *   4. thread 2 가 종료.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   thread_set_priority() 안에서 현재 스레드의 새 우선순위가
 *   ready 큐의 max 보다 낮아지면 thread_yield() 를 호출해야 한다.
 *
 * --- 구현 힌트 (Priority Scheduling 과제) ---
 *
 *   void thread_set_priority (int new_priority) {
 *     thread_current ()->priority = new_priority;
 *     // 새 priority 가 ready 의 max 보다 낮으면 즉시 양보
 *     if (!list_empty (&ready_list)) {
 *       struct thread *top = list_entry (list_max (&ready_list,
 *                                                  priority_less, NULL),
 *                                        struct thread, elem);
 *       if (top->priority > new_priority)
 *         thread_yield ();
 *     }
 *   }
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/thread.h"

static thread_func changing_thread;

void
test_priority_change (void) 
{
  /* MLFQS 에서는 thread_set_priority 가 무시되므로 동작 안 함. */
  ASSERT (!thread_mlfqs);

  msg ("Creating a high-priority thread 2.");
  /* PRI_DEFAULT+1 → preempt 로 즉시 실행됨. */
  thread_create ("thread 2", PRI_DEFAULT + 1, changing_thread, NULL);
  /* 여기 도달 = thread 2 가 우선순위를 낮춰 메인에게 양보한 직후. */
  msg ("Thread 2 should have just lowered its priority.");
  /* 메인 우선순위를 PRI_DEFAULT-2 로 낮춤 → thread 2 (PRI_DEFAULT-1) 보다 낮음
     → 메인이 즉시 양보 → thread 2 가 종료까지 실행. */
  thread_set_priority (PRI_DEFAULT - 2);
  /* 여기 도달 = thread 2 가 종료된 직후. */
  msg ("Thread 2 should have just exited.");
}

static void
changing_thread (void *aux UNUSED) 
{
  msg ("Thread 2 now lowering priority.");
  /* 자기 우선순위를 PRI_DEFAULT-1=30 으로 낮춤.
     이 순간 메인 (31) > thread 2 (30) → thread 2 가 즉시 양보해야 함. */
  thread_set_priority (PRI_DEFAULT - 1);
  /* 여기 도달 = 메인이 자기 우선순위를 낮춰 다시 thread 2 에게 양보한 후. */
  msg ("Thread 2 exiting.");
}
