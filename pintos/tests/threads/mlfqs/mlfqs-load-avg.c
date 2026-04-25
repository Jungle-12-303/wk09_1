/* ============================================================
 * mlfqs-load-avg.c — 시간차로 시작하는 60 개 부하 스레드의 load_avg
 *
 * --- mlfqs-load-60 과의 차이 ---
 *
 *   load-60: 모든 스레드가 같은 시각 (10 초) 에 시작 → 정렬된 부하 변화.
 *   load-avg: 스레드 #i 가 (10+i) 초에 시작 → 점진적 부하 증가.
 *
 *   부하가 천천히 쌓이고 천천히 사라지는 패턴 → load_avg 의 부드러운
 *   곡선 검증.
 *
 * --- 시나리오 ---
 *
 *   60 개 스레드를 한 번에 생성. 각각:
 *     1. (10+i) 초간 sleep → i 가 클수록 늦게 시작.
 *     2. 60 초간 busy.
 *     3. 총 120 초가 경과할 때까지 sleep.
 *
 *   메인은 매 2 초마다 load_avg 출력 (10 초 후 부터).
 *
 * --- 디버깅 팁 (PintOS 가이드 인용) ---
 *
 *   이 테스트만 실패하면 timer_interrupt 핸들러가 너무 무거운 것일
 *   가능성. 인터럽트 안에서 무거운 계산을 하면:
 *     - 메인이 print 후 sleep 으로 돌아갈 시간 부족
 *     - 다음 tick 에 메인이 ready 상태로 잡혀 load_avg 가 인위적으로 상승
 *
 *   해결: 인터럽트 안 작업 최소화. 무거운 계산은 별도 worker 또는
 *         지연 평가 (lazy) 패턴으로.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. 점진적 ready 증가에 따른 load_avg 의 부드러운 상승.
 *   2. 점진적 sleep 종료에 따른 부드러운 하강.
 *   3. 인터럽트 핸들러 효율성 (cf. mlfqs-load-60 보다 민감).
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static int64_t start_time;

static void load_thread (void *seq_no);

#define THREAD_CNT 60

void
test_mlfqs_load_avg (void) 
{
  int i;
  
  ASSERT (thread_mlfqs);

  start_time = timer_ticks ();
  msg ("Starting %d load threads...", THREAD_CNT);
  /* 60 개 부하 스레드 생성. seq_no = 0..59 를 인자로 전달.
     각 스레드의 시작 시점이 다름. */
  for (i = 0; i < THREAD_CNT; i++) 
    {
      char name[16];
      snprintf(name, sizeof name, "load %d", i);
      thread_create (name, PRI_DEFAULT, load_thread, (void *) i);
    }
  msg ("Starting threads took %d seconds.",
       timer_elapsed (start_time) / TIMER_FREQ);
  /* 메인을 nice -20 으로 설정 → 가장 높은 우선순위 → 측정 정확도 보장. */
  thread_set_nice (-20);

  /* 매 2 초마다 90 회 load_avg 출력. */
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

/* 각 부하 스레드.
 *
 * seq_no 가 0 이면 10 초 후 시작, 1 이면 11 초 후 시작, ..., 59 이면 69 초.
 * busy 시간은 60 초 (THREAD_CNT 초 = 60 초).
 * exit_time = 120 초 (THREAD_CNT*2 초). */
static void
load_thread (void *seq_no_) 
{
  int seq_no = (int) seq_no_;
  int sleep_time = TIMER_FREQ * (10 + seq_no);
  int spin_time = sleep_time + TIMER_FREQ * THREAD_CNT;
  int exit_time = TIMER_FREQ * (THREAD_CNT * 2);

  timer_sleep (sleep_time - timer_elapsed (start_time));
  while (timer_elapsed (start_time) < spin_time)
    continue;
  timer_sleep (exit_time - timer_elapsed (start_time));
}
