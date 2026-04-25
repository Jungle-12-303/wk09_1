/* ============================================================
 * mlfqs-block.c — block 된 스레드의 recent_cpu 와 priority 갱신 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   block 된 (lock 대기 등으로 잠든) 스레드도 매 1 초마다 recent_cpu 가
 *   감쇠해야 한다. 그래야 오랜 시간 후 unblock 됐을 때 priority 가
 *   적절히 회복되어 즉시 스케줄될 수 있다.
 *
 * --- 시나리오 ---
 *
 *   1. 메인이 lock 획득.
 *   2. block 스레드 생성 → 20 초간 busy → recent_cpu 누적 → priority 하락.
 *   3. block 스레드가 lock 획득 시도 → 메인이 잡고 있어 10 초간 BLOCKED.
 *   4. 그 10 초 동안:
 *      - 메인은 5 초 더 busy → 메인의 priority 도 변동.
 *      - block 스레드는 BLOCKED 상태이지만 recent_cpu 는 매 초 감쇠해야 함.
 *   5. 메인이 lock 해제 → block 스레드 unblock.
 *      이때 block 의 recent_cpu 가 충분히 감쇠해 있어야 priority 가 메인보다
 *      높아져 **즉시 스케줄** 됨.
 *
 * --- 핵심 검증 포인트 ---
 *
 *   recent_cpu 감쇠는 **모든 스레드** (running + ready + blocked) 에 적용.
 *   blocked 스레드를 빠뜨리면 long-blocked 스레드가 unblock 후에도 낡은
 *   priority 로 남아 starvation 위험.
 *
 * --- 구현 힌트 ---
 *
 *   매 1 초 갱신 루틴:
 *
 *   void update_all_recent_cpu (void) {
 *     thread_foreach_all (update_one_recent_cpu, NULL);
 *     // ↑ all_list 등 전체 스레드 (block 포함) 를 순회
 *   }
 *
 *   PintOS 에는 thread_foreach() 가 있다. blocked 도 포함되는지 확인.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void block_thread (void *lock_);

void
test_mlfqs_block (void) 
{
  int64_t start_time;
  struct lock lock;
  
  ASSERT (thread_mlfqs);

  msg ("Main thread acquiring lock.");
  lock_init (&lock);
  lock_acquire (&lock);
  
  /* block 스레드 생성. 메인은 25 초 sleep → 그동안 block 스레드가 busy. */
  msg ("Main thread creating block thread, sleeping 25 seconds...");
  thread_create ("block", PRI_DEFAULT, block_thread, &lock);
  timer_sleep (25 * TIMER_FREQ);

  /* 메인이 5 초 추가로 busy → 메인의 recent_cpu 도 누적. */
  msg ("Main thread spinning for 5 seconds...");
  start_time = timer_ticks ();
  while (timer_elapsed (start_time) < 5 * TIMER_FREQ)
    continue;

  /* lock 해제 → block 스레드의 lock_acquire 가 성공.
     이때 block 스레드의 priority 가 메인보다 높으면 즉시 preempt. */
  msg ("Main thread releasing lock.");
  lock_release (&lock);

  /* 이 메시지가 block 스레드의 "got it" 보다 뒤에 나오면
     block 의 priority 가 충분히 회복돼 즉시 preempt 됐다는 의미 → PASS. */
  msg ("Block thread should have already acquired lock.");
}

/* block 스레드 진입점.
 *
 * 흐름:
 *   1. 20 초간 busy → recent_cpu 누적 → priority 하락.
 *   2. lock_acquire → BLOCKED (메인이 lock 보유 중).
 *   3. 메인이 5 초 후 sleep 끝나고 5 초 더 busy → 총 10 초 BLOCKED.
 *   4. 이 10 초 동안 recent_cpu 가 감쇠해야 priority 가 회복됨.
 *   5. 메인이 lock 해제 → 깨어남 → priority 가 메인보다 높으면 즉시 실행. */
static void
block_thread (void *lock_) 
{
  struct lock *lock = lock_;
  int64_t start_time;

  /* 20 초 busy → recent_cpu 누적 → priority 낮아짐. */
  msg ("Block thread spinning for 20 seconds...");
  start_time = timer_ticks ();
  while (timer_elapsed (start_time) < 20 * TIMER_FREQ)
    continue;

  /* lock 획득 시도 → BLOCKED 상태로 약 10 초간 대기. */
  msg ("Block thread acquiring lock...");
  lock_acquire (lock);

  /* 깨어남. recent_cpu 가 잘 감쇠됐다면 메인보다 높은 priority 라
     이 메시지가 메인의 "should have already acquired" 보다 먼저 출력됨. */
  msg ("...got it.");
}
