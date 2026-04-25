/* ============================================================
 * alarm-zero.c — timer_sleep(0) 엣지 케이스 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   timer_sleep(0) 호출 시 즉시 반환되어야 한다.
 *   잠들지 말고 그냥 return.
 *
 * --- 왜 중요한가 ---
 *
 *   sleep 구현에서 흔한 버그 패턴:
 *     - ticks <= 0 체크 누락 → 무한 대기
 *     - wakeup_tick 계산에서 ticks 가 0 이면 wakeup == now → 즉시 깨우지만
 *       sleep_list 에 잠깐 들어갔다 나오는 비효율
 *
 *   현재 timer_sleep 구현은 while 조건이 false 라 자연스럽게 통과.
 *   thread_block 기반으로 바꿀 때 이 케이스를 명시적으로 early return
 *   처리하지 않으면 미묘한 버그 발생 가능.
 *
 * --- Alarm Clock 과제 권장 ---
 *
 *   timer_sleep 첫 줄에 다음을 추가:
 *     if (ticks <= 0) return;
 *   가장 안전하고 명시적.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void
test_alarm_zero (void) 
{
  timer_sleep (0);   /* 즉시 반환되어야 함. */
  pass ();           /* 여기 도달하면 PASS. */
}
