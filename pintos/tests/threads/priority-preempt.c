/* ============================================================
 * priority-preempt.c — 높은 우선순위 스레드의 즉시 preempt 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   thread_create() 로 현재 실행 중 스레드보다 높은 우선순위의 스레드를
 *   만들면, 새 스레드가 즉시 CPU 를 차지해야 한다 (preemption).
 *
 *   메인이 thread_create 호출 → 새 스레드 (PRI_DEFAULT+1) 생성 →
 *   메인 (PRI_DEFAULT) 이 즉시 양보 → 새 스레드가 끝까지 실행 → 종료 →
 *   그제서야 메인 재개해서 다음 메시지 출력.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   thread_create() 끝에서 새 스레드의 우선순위 > 현재 스레드면
 *   thread_yield() 를 호출해야 한다.
 *
 * --- 구현 힌트 (Priority Scheduling 과제) ---
 *
 *   tid_t thread_create (...) {
 *     ...
 *     thread_unblock (t);   // ready 큐에 추가
 *     // 새 스레드가 더 높으면 즉시 양보
 *     if (t->priority > thread_current ()->priority)
 *       thread_yield ();
 *     return tid;
 *   }
 *
 * --- 출처 ---
 *
 *   Stanford CS 140 (1999, Matt Franklin / Greg Hutchins / Yu Ping Hu).
 *   arens 가 수정.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func simple_thread_func;

void
test_priority_preempt (void) 
{
  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  /* 메인 우선순위가 기본값인지 확인. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  /* 더 높은 우선순위 (PRI_DEFAULT+1) 의 스레드 생성.
     preempt 가 제대로 동작하면, 이 호출이 끝나기 전에 새 스레드가 모두
     실행되고 종료되어야 한다. */
  thread_create ("high-priority", PRI_DEFAULT + 1, simple_thread_func, NULL);
  /* 이 메시지가 새 스레드의 출력보다 뒤에 나와야 PASS. */
  msg ("The high-priority thread should have already completed.");
}

static void 
simple_thread_func (void *aux UNUSED) 
{
  int i;
  
  /* 5 회 반복하며 메시지 출력 + yield. */
  for (i = 0; i < 5; i++) 
    {
      msg ("Thread %s iteration %d", thread_name (), i);
      thread_yield ();
    }
  msg ("Thread %s done!", thread_name ());
}
