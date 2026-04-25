/* ============================================================
 * mlfqs-load-60.c — 60 개 busy 스레드로 load_avg 변화 측정
 *
 * --- 시나리오 ---
 *
 *   60 개 스레드 생성 → 각 스레드는:
 *     1. 10 초간 sleep (시작 정렬용)
 *     2. 60 초간 busy-wait (60 개 모두 ready 상태)
 *     3. 60 초간 sleep (다시 idle 로)
 *
 *   메인은 매 2 초마다 thread_get_load_avg() 를 출력.
 *
 * --- 기대 출력 (참고치, 오차 허용) ---
 *
 *   After 0 sec : 1.00   (방금 메인만 ready, load_avg 초기값)
 *   After 2 sec : 2.95
 *   ...
 *   After 60 sec: 37.48  (피크 근처, 60 개 다 busy)
 *   After 120 sec: 13.67 (모두 sleep, 빠르게 감쇠)
 *   ...
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. 다수 ready 스레드 환경에서 load_avg 정확 계산.
 *   2. busy → idle 전환 시 load_avg 의 빠른 감쇠 (decay).
 *   3. ready_threads 카운트가 thread 생성·종료에 따라 정확히 변함.
 *
 * --- 구현 힌트 ---
 *
 *   thread_tick() 안에서 매 1 초마다 load_avg 갱신:
 *
 *   void update_load_avg (void) {
 *     int ready = list_size(&ready_list)
 *                 + (current != idle_thread ? 1 : 0);
 *     load_avg = (59 * load_avg + ready * F) / 60;
 *     // 여기서 F = fixed-point 1 의 표현
 *   }
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static int64_t start_time;          /* 모든 스레드의 공통 시작 시각. */

static void load_thread (void *aux);

#define THREAD_CNT 60               /* 부하 스레드 수. */

void
test_mlfqs_load_60 (void) 
{
  int i;
  
  ASSERT (thread_mlfqs);

  start_time = timer_ticks ();
  msg ("Starting %d niced load threads...", THREAD_CNT);
  /* 60 개 부하 스레드 생성. nice 값은 각 스레드가 직접 설정. */
  for (i = 0; i < THREAD_CNT; i++) 
    {
      char name[16];
      snprintf(name, sizeof name, "load %d", i);
      thread_create (name, PRI_DEFAULT, load_thread, NULL);
    }
  msg ("Starting threads took %d seconds.",
       timer_elapsed (start_time) / TIMER_FREQ);
  
  /* 매 2 초마다 90 회 (= 180 초) load_avg 출력.
     start_time + 10 초부터 시작. */
  for (i = 0; i < 90; i++) 
    {
      int64_t sleep_until = start_time + TIMER_FREQ * (2 * i + 10);
      int load_avg;
      timer_sleep (sleep_until - timer_ticks ());
      load_avg = thread_get_load_avg ();
      msg ("After %d seconds, load average=%d.%02d.",
           i * 2, load_avg / 100, load_avg % 100);
    }
}

/* 각 부하 스레드 진입점.
 *
 * 흐름:
 *   1. nice = 20 으로 설정 (낮은 우선순위, 동등하게 분배).
 *   2. start + 10 초까지 sleep → 모두 같은 시각에 깨어남.
 *   3. start + 70 초까지 busy-wait (= 60 초간 ready 유지).
 *   4. start + 130 초까지 sleep (= 60 초간 idle).
 */
static void
load_thread (void *aux UNUSED) 
{
  int64_t sleep_time = 10 * TIMER_FREQ;
  int64_t spin_time = sleep_time + 60 * TIMER_FREQ;
  int64_t exit_time = spin_time + 60 * TIMER_FREQ;

  thread_set_nice (20);
  timer_sleep (sleep_time - timer_elapsed (start_time));
  while (timer_elapsed (start_time) < spin_time)
    continue;
  timer_sleep (exit_time - timer_elapsed (start_time));
}
