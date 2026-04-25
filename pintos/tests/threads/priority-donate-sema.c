/* ============================================================
 * priority-donate-sema.c — 락과 세마포어가 함께 있을 때의 donation
 *
 * --- 무엇을 검증하는가 ---
 *
 *   donation 이 락에 대해서만 동작하고, 세마포어에는 적용되지 않음을
 *   확인. 또한 락+세마포어 조합으로도 우선순위 순 wakeup 이 유지됨.
 *
 *   핵심 차이:
 *     - 락은 "owner" 가 명확 → owner 에게 기부 가능.
 *     - 세마포어는 owner 개념이 없음 → 기부 대상이 없음.
 *
 * --- 시나리오 ---
 *
 *   1. 메인 (31) 이 L (32), M (34), H (36) 세 스레드 생성.
 *      세 스레드 모두 ready 큐에 → priority 순으로 H, M, L 순 실행.
 *      그러나 모두 sema_down (value=0) 또는 lock_acquire 에서 블록됨.
 *
 *   2. L 진입: lock 획득 → sema_down 에서 블록.
 *   3. M 진입: sema_down 에서 즉시 블록.
 *   4. H 진입: lock_acquire 시도 → L 이 holder → L 에 36 기부.
 *      L 의 effective priority = 36.
 *
 *   5. 메인이 sema_up → 세마포어 큐에서 L 또는 M 깨어남.
 *      donation 으로 L 의 effective = 36 > M = 34 → L 이 먼저.
 *      L 깨어나서 lock_release → H 깨어나서 sema_up.
 *      M 깨어나서 종료. H 종료. L 종료.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   세마포어 큐에서도 effective priority (donation 받은 후 값) 로
 *   waiter 를 정렬해야 한다. 단순히 originally priority 만 보면 L (32)
 *   가 M (34) 보다 낮아 보이지만, donation 으로 L 의 effective 는 36 이
 *   되어 있어야 한다.
 *
 * --- 출처 ---
 *
 *   Godmar Back <gback@cs.vt.edu>.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 락과 세마포어를 한 인자로 묶기 위한 구조체. */
struct lock_and_sema 
  {
    struct lock lock;
    struct semaphore sema;
  };

static thread_func l_thread_func;
static thread_func m_thread_func;
static thread_func h_thread_func;

void
test_priority_donate_sema (void) 
{
  struct lock_and_sema ls;

  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  /* 메인 우선순위가 기본값인지 확인. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&ls.lock);
  sema_init (&ls.sema, 0);

  /* 세 스레드 생성. priority 순서로 H 가 가장 먼저 실행 → lock_acquire
     에서 L 에게 기부 → 블록. M 도 sema_down 에서 블록. L 도 sema_down 블록.
     순서는 priority 가 높을수록 먼저 실행. */
  thread_create ("low", PRI_DEFAULT + 1, l_thread_func, &ls);
  thread_create ("med", PRI_DEFAULT + 3, m_thread_func, &ls);
  thread_create ("high", PRI_DEFAULT + 5, h_thread_func, &ls);

  /* 메인이 sema_up → 세마포어 waiter 중 effective priority 가장 높은 게 깨어남.
     L (32 + donation 36) 이 M (34) 보다 effective 높으므로 L 이 먼저 깨어남. */
  sema_up (&ls.sema);
  msg ("Main thread finished.");
}

/* L 진입점. lock 획득 → sema_down (블록) → 깨어나면 lock 해제. */
static void
l_thread_func (void *ls_) 
{
  struct lock_and_sema *ls = ls_;

  lock_acquire (&ls->lock);
  msg ("Thread L acquired lock.");
  /* 여기서 블록. 깨어나는 시점에는 H 의 donation 으로 effective = 36. */
  sema_down (&ls->sema);
  msg ("Thread L downed semaphore.");
  /* 락 해제 → H 가 lock 받음 → H 가 다시 sema_up → M 깨어남. */
  lock_release (&ls->lock);
  msg ("Thread L finished.");
}

/* M 진입점. sema_down 에서 블록 → 깨어나면 종료. */
static void
m_thread_func (void *ls_) 
{
  struct lock_and_sema *ls = ls_;

  sema_down (&ls->sema);
  msg ("Thread M finished.");
}

/* H 진입점. lock_acquire 에서 블록 → L 에게 기부 → 깨어나면 sema_up. */
static void
h_thread_func (void *ls_) 
{
  struct lock_and_sema *ls = ls_;

  /* L 이 lock 잡고 있어 블록 → L 에 donation. */
  lock_acquire (&ls->lock);
  msg ("Thread H acquired lock.");

  /* sema_up 으로 M 을 깨움. 자기는 락 해제. */
  sema_up (&ls->sema);
  lock_release (&ls->lock);
  msg ("Thread H finished.");
}
