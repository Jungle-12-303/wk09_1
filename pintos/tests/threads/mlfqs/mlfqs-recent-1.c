/* ============================================================
 * mlfqs-recent-1.c — 단일 ready 프로세스의 recent_cpu 정확성 검증
 *
 * --- recent_cpu 갱신 공식 ---
 *
 *   매 1 초마다:
 *     recent_cpu = (2*load_avg) / (2*load_avg + 1) * recent_cpu + nice
 *   매 tick 마다 (현재 실행 중인 스레드만):
 *     recent_cpu += 1
 *
 *   → CPU 사용량 누적 + 시간에 따른 감쇠.
 *
 * --- 본 테스트의 검증 ---
 *
 *   1 개 스레드 (메인) 만 ready 인 상황에서 recent_cpu 가 시간에 따라
 *   어떻게 변하는지 측정. 기대 출력 (참고치):
 *
 *     After 2 sec : recent_cpu=6.40,  load_avg=0.03
 *     After 10 sec: recent_cpu=30.08, load_avg=0.15
 *     After 60 sec: recent_cpu=125.46, load_avg=0.64
 *     After 180 sec: recent_cpu=189.97, load_avg=0.95 (수렴)
 *
 *   load_avg 가 1 에 가까워질수록 recent_cpu 증가 속도가 줄어듬
 *   (감쇠 계수가 0.5 에 가까워짐).
 *
 * --- 핵심 검증 포인트 ---
 *
 *   1. 매 1 초마다 정확한 시점에 recent_cpu 갱신 (timer_ticks() % TIMER_FREQ == 0).
 *   2. fixed-point 산술의 정밀도 (소수점 둘째자리까지 일치).
 *   3. 단일 스레드 환경에서 매 tick 마다 recent_cpu++ 정확.
 *
 * --- 주의 ---
 *
 *   recent_cpu 갱신 시점은 timer_ticks() % TIMER_FREQ == 0 일 때.
 *   즉 정확한 1 초 경계에서 실행되어야 함. 그래서 do-while 로 정렬.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

/* recent_cpu 갱신이 정확히 timer_ticks() % TIMER_FREQ == 0 일 때 발생한다는
   가정에 민감하다. */

void
test_mlfqs_recent_1 (void) 
{
  int64_t start_time;
  int last_elapsed = 0;
  
  ASSERT (thread_mlfqs);

  /* 시작 전 recent_cpu 가 충분히 낮아질 때까지 대기.
     이전 테스트나 부팅 과정에서 누적된 값이 700 이하로 떨어질 때까지. */
  do 
    {
      msg ("Sleeping 10 seconds to allow recent_cpu to decay, please wait...");
      start_time = timer_ticks ();
      timer_sleep (DIV_ROUND_UP (start_time, TIMER_FREQ) - start_time
                   + 10 * TIMER_FREQ);
    }
  while (thread_get_recent_cpu () > 700);

  /* 측정 루프. 매 2 초마다 (= elapsed 가 TIMER_FREQ*2 의 배수일 때)
     recent_cpu 와 load_avg 를 출력. 180 초까지 진행. */
  start_time = timer_ticks ();
  for (;;) 
    {
      int elapsed = timer_elapsed (start_time);
      if (elapsed % (TIMER_FREQ * 2) == 0 && elapsed > last_elapsed) 
        {
          int recent_cpu = thread_get_recent_cpu ();
          int load_avg = thread_get_load_avg ();
          int elapsed_seconds = elapsed / TIMER_FREQ;
          msg ("After %d seconds, recent_cpu is %d.%02d, load_avg is %d.%02d.",
               elapsed_seconds,
               recent_cpu / 100, recent_cpu % 100,
               load_avg / 100, load_avg % 100);
          if (elapsed_seconds >= 180)
            break;
        } 
      last_elapsed = elapsed;
    }
}
