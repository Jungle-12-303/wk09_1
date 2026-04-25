/* ============================================================
 * alarm-simultaneous.c — 같은 시각에 여러 스레드가 깨어나는지 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   3 개 스레드가 모두 동일 시각에 깨어나도록 timer_sleep 을 호출.
 *   각 iteration 에서 모든 스레드가 같은 tick 에 깨어나면 PASS.
 *
 *   각 스레드는 매번 10 틱 sleep 후 깨어남:
 *     iteration 1 → start + 10 틱에 모두 깨어남
 *     iteration 2 → start + 20 틱에 모두 깨어남
 *     iteration 3 → start + 30 틱에 모두 깨어남
 *     ...
 *
 * --- 핵심 차이 (alarm-multiple 과의) ---
 *
 *   alarm-multiple : 각 스레드 duration 다름 → 깨어나는 시각이 다름
 *   alarm-simultaneous : 모든 스레드 duration 같음 → 같은 시각 wake-up
 *
 *   같은 시각 wake-up 이 보장되어야 한다는 게 핵심 검증 포인트.
 *   timer_sleep 구현이 같은 wakeup_tick 을 가진 스레드들을 한 번에
 *   깨우지 못하면 실패.
 *
 * --- Alarm Clock 과제 관점 ---
 *
 *   thread_block / thread_unblock 기반 구현에서:
 *     timer_interrupt 가 매 틱 sleep_list 를 검사 → 깨어날 시각 도달
 *     스레드들을 모두 unblock.
 *   이때 sleep_list 에서 같은 wakeup_tick 을 가진 스레드들이
 *   한꺼번에 처리돼야 한다.
 *
 * --- 출력 형식 ---
 *
 *   "iteration X, thread Y: woke up Z ticks later"
 *   같은 iteration 내에서 Z 가 0 이어야 (= 동시 깨어남) 함.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep (int thread_cnt, int iterations);

/* alarm-simultaneous: 3 스레드 × 5 회 동시 sleep. */
void
test_alarm_simultaneous (void) 
{
  test_sleep (3, 5);
}

/* 모든 sleep 스레드가 공유하는 정보.
   alarm-wait 과 다른 점: 락이 없다 — output 은 단순 누적이라 동시
   기록이 충돌해도 큰 문제 없는 시나리오. */
struct sleep_test 
  {
    int64_t start;             /* 테스트 시작 시각. */
    int iterations;            /* 각 스레드 sleep 횟수. */
    int *output_pos;           /* 출력 버퍼의 다음 기록 위치. */
  };

static void sleeper (void *);

/* THREAD_CNT 개 스레드가 ITERATIONS 회 sleep 한다. */
static void
test_sleep (int thread_cnt, int iterations) 
{
  struct sleep_test test;
  int *output;
  int i;

  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  msg ("Creating %d threads to sleep %d times each.", thread_cnt, iterations);
  msg ("Each thread sleeps 10 ticks each time.");
  msg ("Within an iteration, all threads should wake up on the same tick.");

  /* 출력 버퍼 할당. */
  output = malloc (sizeof *output * iterations * thread_cnt * 2);
  if (output == NULL)
    PANIC ("couldn't allocate memory for test");

  /* 테스트 컨텍스트. start 에 100 틱 여유를 둠. */
  test.start = timer_ticks () + 100;
  test.iterations = iterations;
  test.output_pos = output;

  /* N 개 sleep 스레드 생성. 모두 같은 sleeper 함수, 같은 컨텍스트. */
  ASSERT (output != NULL);
  for (i = 0; i < thread_cnt; i++)
    {
      char name[16];
      snprintf (name, sizeof name, "thread %d", i);
      thread_create (name, PRI_DEFAULT, sleeper, &test);
    }
  
  /* 모든 스레드가 끝날 때까지 메인이 대기.
     매 iteration 10 틱 × iterations 회 + 시작·종료 여유 100 틱씩. */
  timer_sleep (100 + iterations * 10 + 100);

  /* 깨어난 시점들을 출력.
     output[i] = "i 번째 wakeup 시점 - test.start" (틱 차이).
     같은 iteration 내 스레드들의 출력이 0 이어야 (= 즉시 깨어남) 정상. */
  msg ("iteration 0, thread 0: woke up after %d ticks", output[0]);
  for (i = 1; i < test.output_pos - output; i++) 
    msg ("iteration %d, thread %d: woke up %d ticks later",
         i / thread_cnt, i % thread_cnt, output[i] - output[i - 1]);
  
  free (output);
}

/* 개별 sleep 스레드 진입점.
 *
 * 흐름:
 *   1. timer_sleep(1) 로 다음 tick 의 시작 지점에 정확히 정렬.
 *      이걸 안 하면 첫 sleep 이 부분 tick 에서 시작해 정확도 떨어짐.
 *   2. iteration 마다 sleep_until = start + i*10 까지 자고 깨어남.
 *   3. 깨어난 절대 시각 (start 기준 오프셋) 기록.
 *   4. thread_yield() 로 다른 스레드도 출력 기회 보장.
 */
static void
sleeper (void *test_) 
{
  struct sleep_test *test = test_;
  int i;

  /* tick 경계에 정렬 (1 틱 sleep). */
  timer_sleep (1);

  for (i = 1; i <= test->iterations; i++) 
    {
      int64_t sleep_until = test->start + i * 10;
      timer_sleep (sleep_until - timer_ticks ());
      *test->output_pos++ = timer_ticks () - test->start;
      thread_yield ();
    }
}
