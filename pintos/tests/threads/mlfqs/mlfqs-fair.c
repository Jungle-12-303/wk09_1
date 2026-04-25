/* ============================================================
 * mlfqs-fair.c — 4 개 테스트 함수의 본체 (fair-2, fair-20, nice-2, nice-10)
 *
 * --- 4 개 테스트의 공통 구조 ---
 *
 *   N 개 스레드를 만들어 30 초간 부하를 주고, 각 스레드가 받은 tick 수를
 *   측정. 스케줄러의 공정성과 nice 값에 따른 차등을 검증.
 *
 *   30 초 × 100 tick/sec ≈ 3000 tick 이 N 개 스레드에 분배됨.
 *
 * --- 4 개 시나리오 ---
 *
 *   test_mlfqs_fair_2  : 2 스레드, 모두 nice=0  → 각각 ~1500 tick (균등)
 *   test_mlfqs_fair_20 : 20 스레드, 모두 nice=0 → 각각 ~150 tick (균등)
 *   test_mlfqs_nice_2  : 2 스레드, nice=0 / 5  → 1904 / 1096 tick (차등)
 *   test_mlfqs_nice_10 : 10 스레드, nice=0..9  → 672 / 588 / .. / 8 tick
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. **fair**: 동일 nice 의 스레드들이 거의 같은 CPU 시간을 받음.
 *   2. **nice**: nice 가 높을수록 (= 양보 의지가 큼) 더 적은 CPU.
 *
 *   priority 공식 priority = PRI_MAX - (recent_cpu/4) - (nice*2) 에서:
 *     - 같은 nice → recent_cpu 가 비슷해서 priority 도 비슷 → 균등
 *     - 다른 nice → 직접 priority 영향 + recent_cpu 갱신에도 +nice 가 들어가
 *       나비효과로 차이가 누적
 *
 * --- 출처 ---
 *
 *   기대 tick 수치는 시뮬레이션 (mlfqs.pm) 으로 산출.
 * ============================================================ */

#include <stdio.h>
#include <inttypes.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_mlfqs_fair (int thread_cnt, int nice_min, int nice_step);

/* fair-2: 2 스레드, nice 모두 0. */
void
test_mlfqs_fair_2 (void) 
{
  test_mlfqs_fair (2, 0, 0);
}

/* fair-20: 20 스레드, nice 모두 0. */
void
test_mlfqs_fair_20 (void) 
{
  test_mlfqs_fair (20, 0, 0);
}

/* nice-2: 2 스레드, nice 0 / 5. */
void
test_mlfqs_nice_2 (void) 
{
  test_mlfqs_fair (2, 0, 5);
}

/* nice-10: 10 스레드, nice 0..9 (1 씩 증가). */
void
test_mlfqs_nice_10 (void) 
{
  test_mlfqs_fair (10, 0, 1);
}

#define MAX_THREAD_CNT 20

/* 각 부하 스레드의 측정 정보. */
struct thread_info 
  {
    int64_t start_time;       /* 공통 시작 시각. */
    int tick_count;           /* 이 스레드가 실제로 실행된 tick 수. */
    int nice;                 /* 이 스레드의 nice 값. */
  };

static void load_thread (void *aux);

/* 공통 본체.
 *
 * thread_cnt 개 스레드 생성, 각각 nice = nice_min + i*nice_step.
 * 40 초 sleep 후 각 스레드의 tick_count 를 출력. */
static void
test_mlfqs_fair (int thread_cnt, int nice_min, int nice_step)
{
  struct thread_info info[MAX_THREAD_CNT];
  int64_t start_time;
  int nice;
  int i;

  ASSERT (thread_mlfqs);
  ASSERT (thread_cnt <= MAX_THREAD_CNT);
  ASSERT (nice_min >= -10);
  ASSERT (nice_step >= 0);
  ASSERT (nice_min + nice_step * (thread_cnt - 1) <= 20);

  /* 메인을 가장 우선순위 높게 (-20) → 측정 작업이 정확히 진행. */
  thread_set_nice (-20);

  start_time = timer_ticks ();
  msg ("Starting %d threads...", thread_cnt);
  nice = nice_min;
  /* 부하 스레드 생성. nice 값을 점진적으로 증가시키며 부여. */
  for (i = 0; i < thread_cnt; i++) 
    {
      struct thread_info *ti = &info[i];
      char name[16];

      ti->start_time = start_time;
      ti->tick_count = 0;
      ti->nice = nice;

      snprintf(name, sizeof name, "load %d", i);
      thread_create (name, PRI_DEFAULT, load_thread, ti);

      nice += nice_step;
    }
  msg ("Starting threads took %"PRId64" ticks.", timer_elapsed (start_time));

  /* 부하 스레드들이 충분히 동작하도록 40 초 대기. */
  msg ("Sleeping 40 seconds to let threads run, please wait...");
  timer_sleep (40 * TIMER_FREQ);
  
  /* 각 스레드가 받은 tick 수 출력. */
  for (i = 0; i < thread_cnt; i++)
    msg ("Thread %d received %d ticks.", i, info[i].tick_count);
}

/* 각 부하 스레드 진입점.
 *
 * 흐름:
 *   1. nice 값 설정.
 *   2. start + 5 초까지 sleep (모든 스레드 일제히 시작 정렬).
 *   3. start + 35 초까지 busy. 매 새로운 tick 마다 tick_count++ 로
 *      자기가 실제로 받은 CPU 시간 측정.
 *
 * 핵심: cur_time != last_time 으로 "새로운 tick 시작" 을 감지.
 *       이 측정 방식이 측정 자체로 부하를 일으키지 않게 가벼움. */
static void
load_thread (void *ti_) 
{
  struct thread_info *ti = ti_;
  int64_t sleep_time = 5 * TIMER_FREQ;
  int64_t spin_time = sleep_time + 30 * TIMER_FREQ;
  int64_t last_time = 0;

  thread_set_nice (ti->nice);
  timer_sleep (sleep_time - timer_elapsed (ti->start_time));
  while (timer_elapsed (ti->start_time) < spin_time) 
    {
      int64_t cur_time = timer_ticks ();
      if (cur_time != last_time)
        ti->tick_count++;
      last_time = cur_time;
    }
}
