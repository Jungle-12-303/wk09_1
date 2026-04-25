/* ============================================================
 * priority-condvar.c — 조건 변수 시그널의 우선순위 정렬 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   여러 스레드가 cond_wait() 로 한 조건 변수에서 대기 중일 때,
 *   cond_signal() 이 가장 높은 우선순위 waiter 를 깨워야 한다.
 *
 *   시나리오:
 *     1. 메인이 PRI_MIN 으로 우선순위 낮춤.
 *     2. 10 개 스레드를 우선순위 21~30 으로 생성.
 *        각자 lock 획득 → cond_wait → 깨어나면 메시지 출력 → lock 해제.
 *     3. 메인이 lock 획득 → cond_signal → lock 해제 를 10 회 반복.
 *        매번 가장 높은 우선순위 waiter 가 깨어나야 함.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   조건 변수의 waiter 는 각자 별개의 세마포어 (waiter list 의 element)
 *   를 갖는다. cond_signal 은 "어떤 waiter 의 세마포어를 sema_up 할지"
 *   결정해야 하는데, 이때 waiter 리스트에서 가장 높은 우선순위의
 *   스레드를 가진 항목을 골라야 한다.
 *
 * --- 구현 힌트 (Priority Scheduling 과제) ---
 *
 *   cond_signal 안에서 waiter 리스트를 순회하며 max priority 찾기.
 *   각 waiter 의 priority 는 그 sema 의 첫 번째 (유일한) waiter 의
 *   priority 로 결정 (cond_wait 호출 직후이므로).
 *
 *   void cond_signal (struct condition *cond, struct lock *lock UNUSED) {
 *     ...
 *     if (!list_empty (&cond->waiters)) {
 *       struct list_elem *e = list_max (&cond->waiters,
 *                                       sema_priority_less, NULL);
 *       list_remove (e);
 *       sema_up (&list_entry (e, struct semaphore_elem, elem)->semaphore);
 *     }
 *   }
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func priority_condvar_thread;
static struct lock lock;
static struct condition condition;

void
test_priority_condvar (void) 
{
  int i;
  
  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  lock_init (&lock);
  cond_init (&condition);

  /* 메인을 PRI_MIN 으로 낮춤 → 모든 sleeper 가 더 높음. */
  thread_set_priority (PRI_MIN);

  /* 10 개 sleeper 생성. 우선순위 = 30,29,...,21 의 섞인 순서.
     priority = PRI_DEFAULT - (i+7)%10 - 1, i=0..9. */
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 7) % 10 - 1;
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, priority_condvar_thread, NULL);
    }

  /* 시그널을 10 회 보냄. 매번 가장 높은 우선순위 waiter 가 깨어나야 함.
     출력 순서: 30 → 29 → ... → 21 순으로 "woke up" 이 나와야 PASS. */
  for (i = 0; i < 10; i++) 
    {
      lock_acquire (&lock);
      msg ("Signaling...");
      cond_signal (&condition, &lock);
      lock_release (&lock);
    }
}

/* 각 sleeper 진입점.
 *
 * 흐름:
 *   1. 시작 메시지 출력 ("Thread X starting.")
 *   2. lock 획득 → cond_wait (lock 자동 해제 + 블록)
 *   3. 깨어나면 (lock 자동 재획득) "woke up" 메시지 출력
 *   4. lock 해제
 *
 * cond_signal 이 우선순위 순서로 깨워야 출력도 우선순위 순서. */
static void
priority_condvar_thread (void *aux UNUSED) 
{
  msg ("Thread %s starting.", thread_name ());
  lock_acquire (&lock);
  cond_wait (&condition, &lock);
  msg ("Thread %s woke up.", thread_name ());
  lock_release (&lock);
}
