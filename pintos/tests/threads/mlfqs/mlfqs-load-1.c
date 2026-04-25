/* ============================================================
 * mlfqs-load-1.c — load_avg 가 1 개 busy 스레드로 0.5 까지 오르는 시간 검증
 *
 * --- MLFQS (4.4BSD Scheduler) 핵심 ---
 *
 *   priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
 *   recent_cpu = (2*load_avg) / (2*load_avg + 1) * recent_cpu + nice
 *   load_avg = (59/60) * load_avg + (1/60) * ready_threads
 *
 *   - load_avg 는 매 1 초마다 갱신.
 *   - recent_cpu 는 매 1 초마다 갱신.
 *   - priority 는 매 4 tick 마다 재계산.
 *
 * --- 본 테스트의 검증 ---
 *
 *   1 개의 busy 스레드 (메인) 가 계속 ready 상태이면, load_avg 는
 *   ready_threads = 1 로 매 초 (59/60)*L + 1/60 으로 갱신.
 *   L 이 0.5 까지 오르는 시간:
 *
 *     perl -e '$i++,$a=(59*$a+1)/60while$a<=.5;print "$i\n"'
 *     → 약 42 초.
 *
 *   허용 범위: 38 ~ 45 초.
 *
 *   그 다음 10 초 동안 inactive (메인이 timer_sleep) 하면 ready_threads = 0
 *   이 되어 load_avg 가 다시 0.5 미만으로 떨어져야 한다.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. load_avg 의 1 초 단위 정확한 갱신.
 *   2. ready_threads 카운트 정확성 (idle 제외, 자기 자신 포함).
 *   3. fixed-point 산술의 정밀도.
 *
 * --- 출력 형식 ---
 *
 *   load_avg 는 100 배 정수로 표현 (예: 50 = 0.50).
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void
test_mlfqs_load_1 (void) 
{
  int64_t start_time;
  int elapsed;
  int load_avg;
  
  ASSERT (thread_mlfqs);

  msg ("spinning for up to 45 seconds, please wait...");

  /* busy-wait 로 load_avg 가 0.5 (= 50) 를 넘을 때까지 측정. */
  start_time = timer_ticks ();
  for (;;) 
    {
      load_avg = thread_get_load_avg ();
      ASSERT (load_avg >= 0);
      elapsed = timer_elapsed (start_time) / TIMER_FREQ;
      if (load_avg > 100)
        fail ("load average is %d.%02d "
              "but should be between 0 and 1 (after %d seconds)",
              load_avg / 100, load_avg % 100, elapsed);
      else if (load_avg > 50)
        break;          /* 0.5 도달 = 정상 */
      else if (elapsed > 45)
        fail ("load average stayed below 0.5 for more than 45 seconds");
    }

  /* 너무 빨리 (38 초 미만) 도달해도 잘못된 것. */
  if (elapsed < 38)
    fail ("load average took only %d seconds to rise above 0.5", elapsed);
  msg ("load average rose to 0.5 after %d seconds", elapsed);

  /* 10 초간 sleep → ready_threads=0 → load_avg 가 다시 0.5 미만으로. */
  msg ("sleeping for another 10 seconds, please wait...");
  timer_sleep (TIMER_FREQ * 10);

  load_avg = thread_get_load_avg ();
  if (load_avg < 0)
    fail ("load average fell below 0");
  if (load_avg > 50)
    fail ("load average stayed above 0.5 for more than 10 seconds");
  msg ("load average fell back below 0.5 (to %d.%02d)",
       load_avg / 100, load_avg % 100);

  pass ();
}
