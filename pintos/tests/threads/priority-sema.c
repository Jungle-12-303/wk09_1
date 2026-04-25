/* ============================================================
 * priority-sema.c — 세마포어 대기 큐의 우선순위 정렬 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   여러 스레드가 sema_down() 으로 한 세마포어에서 대기 중일 때,
 *   sema_up() 이 이 중 가장 높은 우선순위 스레드를 깨워야 한다.
 *
 *   시나리오:
 *     1. 메인이 자기 우선순위를 PRI_MIN 으로 낮춤.
 *     2. 10 개 스레드를 다양한 우선순위 (21~30) 로 생성.
 *        모두 sema_down(0) 으로 즉시 블록됨.
 *     3. 메인이 sema_up() 을 10 회 호출.
 *        매 호출마다 가장 높은 우선순위 스레드가 깨어나야 한다.
 *        → 출력 순서: 30, 29, 28, ..., 21
 *
 * --- 핵심 검증 포인트 ---
 *
 *   sema_up() 안에서 waiter 큐의 첫 번째가 아니라,
 *   가장 높은 우선순위 waiter 를 unblock 해야 한다.
 *
 * --- 구현 힌트 (Priority Scheduling 과제) ---
 *
 *   기본 sema_up 은 list_pop_front 를 쓴다 (FIFO). 이를 우선순위 기반으로:
 *
 *   void sema_up (struct semaphore *sema) {
 *     enum intr_level old_level = intr_disable ();
 *     if (!list_empty (&sema->waiters)) {
 *       // 가장 높은 우선순위 waiter 찾기
 *       struct list_elem *e = list_max (&sema->waiters,
 *                                       priority_less, NULL);
 *       list_remove (e);
 *       thread_unblock (list_entry (e, struct thread, elem));
 *     }
 *     sema->value++;
 *     // 깨어난 스레드가 현재보다 높으면 즉시 양보
 *     thread_yield_if_higher_in_ready ();
 *     intr_set_level (old_level);
 *   }
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func priority_sema_thread;
static struct semaphore sema;   /* value=0 으로 초기화, 모든 sleeper 가 블록됨. */

void
test_priority_sema (void) 
{
  int i;
  
  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  sema_init (&sema, 0);
  /* 메인을 PRI_MIN 으로 낮춤 → sleeper 들이 메인보다 무조건 높음.
     깨어나면 즉시 실행되도록. */
  thread_set_priority (PRI_MIN);

  /* 10 개 sleeper 를 다양한 우선순위로 생성.
     priority = PRI_DEFAULT - (i+3)%10 - 1, i=0..9 → 30,29,...,21 의 섞인 순서.
     생성 직후 sema_down 으로 모두 블록됨. */
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 3) % 10 - 1;
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, priority_sema_thread, NULL);
    }

  /* sema_up 을 10 회 호출. 매번 가장 높은 우선순위 waiter 가 깨어나야 함.
     출력은 30 → 29 → 28 → ... → 21 순서로 나와야 PASS. */
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema);
      msg ("Back in main thread."); 
    }
}

/* 각 sleeper 진입점. sema_down 후 깨어나면 자기 이름 출력. */
static void
priority_sema_thread (void *aux UNUSED) 
{
  sema_down (&sema);
  msg ("Thread %s woke up.", thread_name ());
}
