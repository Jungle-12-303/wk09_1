/* ============================================================
 * priority-donate-chain.c — 깊이 8 단계의 nested donation 종합 검증
 *
 * --- 개요 ---
 *
 *   priority-donate-nest 의 일반화.
 *   8 단계 chain 을 만들어 가장 깊은 donation 까지 정확히 전파되는지
 *   검증한다.
 *
 * --- 시나리오 ---
 *
 *   1. 메인이 자기 priority 를 PRI_MIN 으로 낮춤.
 *   2. 7 개 donor thread 생성: thread 1..7 with priority PRI_MIN+3, 6, ..., 21.
 *   3. 메인이 lock 0..7 (8 개) 초기화 → lock 0 획득.
 *   4. thread[i] 시작 시:
 *        - lock[i] 획득 (i < 7 일 때만)
 *        - lock[i-1] 획득 시도 → thread[i-1] 이 잡고 있음 → 기부.
 *      → chain donation 이 메인까지 전파.
 *   5. 모두 블록되면 메인이 lock 0 해제 → thread 1 이 깨어남 → ...
 *      (계단식으로 풀림)
 *   6. 동시에 interloper 스레드 (PRI_MIN+2, 5, 8, ..., 20) 도 만들어서,
 *      donor 스레드 (PRI_MIN+3, 6, 9, ..., 21) 가 끝나야 interloper 가
 *      실행되는지 (= 우선순위 정렬) 검증.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. **깊이 8 의 chain donation** 이 정확히 전파.
 *   2. **donor (PRI_MIN+3i) 가 interloper (PRI_MIN+3i-1) 보다 항상 먼저 실행.**
 *   3. 락 해제 순서대로 chain 이 점진적으로 풀림.
 *
 * --- 구현 힌트 ---
 *
 *   재귀 깊이 제한 NESTING_DEPTH=8 정도로 두면 안전.
 *   chain donation 함수:
 *
 *   void donate_priority (struct thread *t, int new_priority, int depth) {
 *     if (depth >= NESTING_DEPTH) return;
 *     if (t->priority >= new_priority) return;
 *     t->priority = new_priority;
 *     if (t->wait_on_lock)
 *       donate_priority (t->wait_on_lock->holder, new_priority, depth + 1);
 *   }
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

#define NESTING_DEPTH 8     /* chain 깊이. 메인 + 7 donor = 8. */

/* donor 스레드가 잡으려는 두 락 정보. */
struct lock_pair
  {
    struct lock *second;    /* 메인 또는 다른 donor 가 잡고 있는 락. */
    struct lock *first;     /* 자기 자신이 먼저 잡을 락 (선택). */
  };

static thread_func donor_thread_func;
static thread_func interloper_thread_func;

void
test_priority_donate_chain (void) 
{
  int i;  
  struct lock locks[NESTING_DEPTH - 1];
  struct lock_pair lock_pairs[NESTING_DEPTH];

  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  /* 메인 priority 를 최저로. donor 들이 자연스럽게 깨어나면 더 높음. */
  thread_set_priority (PRI_MIN);

  /* 7 개 락 초기화. */
  for (i = 0; i < NESTING_DEPTH - 1; i++)
    lock_init (&locks[i]);

  /* 메인이 첫 번째 락 획득 (chain 의 끝). */
  lock_acquire (&locks[0]);
  msg ("%s got lock.", thread_name ());

  /* 7 개의 donor + 7 개의 interloper 생성.
     donor[i]: priority = PRI_MIN + i*3, lock[i] 잡고 lock[i-1] 기다림.
     interloper[i]: priority = PRI_MIN + i*3 - 1, 락 무관. */
  for (i = 1; i < NESTING_DEPTH; i++)
    {
      char name[16];
      int thread_priority;

      snprintf (name, sizeof name, "thread %d", i);
      thread_priority = PRI_MIN + i * 3;
      lock_pairs[i].first = i < NESTING_DEPTH - 1 ? locks + i: NULL;
      lock_pairs[i].second = locks + i - 1;

      /* donor 생성. lock_acquire(first) → lock_acquire(second) 시도.
         second 가 잡혀있으니 chain donation 발생. */
      thread_create (name, thread_priority, donor_thread_func, lock_pairs + i);
      /* 매번 메인의 effective priority 가 새 donor 의 priority 까지 올라감. */
      msg ("%s should have priority %d.  Actual priority: %d.",
          thread_name (), thread_priority, thread_get_priority ());

      /* interloper: 같은 priority 영역의 한 단계 낮은 값.
         donor 가 끝나야 실행되어야 함. */
      snprintf (name, sizeof name, "interloper %d", i);
      thread_create (name, thread_priority - 1, interloper_thread_func, NULL);
    }

  /* 메인이 lock[0] 해제 → thread 1 이 받음 → thread 1 이 진행 →
     thread 2 가 받음 → ... 계단식으로 풀림. */
  lock_release (&locks[0]);
  msg ("%s finishing with priority %d.", thread_name (),
                                         thread_get_priority ());
}

/* donor 진입점.
 *
 * 흐름:
 *   1. (i < 7 이면) lock[i] 획득.
 *   2. lock[i-1] 획득 시도 → 위에서 잡고 있어 블록 → chain donation 발생.
 *   3. 깨어나면 (앞의 chain 이 풀려서) 메시지 출력.
 *   4. lock[i-1] 해제, lock[i] 해제.
 */
static void
donor_thread_func (void *locks_) 
{
  struct lock_pair *locks = locks_;

  if (locks->first)
    lock_acquire (locks->first);

  /* 이 lock_acquire 에서 chain donation 이 위로 전파됨. */
  lock_acquire (locks->second);
  msg ("%s got lock", thread_name ());

  lock_release (locks->second);
  msg ("%s should have priority %d. Actual priority: %d", 
        thread_name (), (NESTING_DEPTH - 1) * 3,
        thread_get_priority ());

  if (locks->first)
    lock_release (locks->first);

  msg ("%s finishing with priority %d.", thread_name (),
                                         thread_get_priority ());
}

/* interloper 진입점. 락과 무관 — 그냥 자기 priority 차례에 실행되면 종료. */
static void
interloper_thread_func (void *arg_ UNUSED)
{
  msg ("%s finished.", thread_name ());
}

// vim: sw=2
