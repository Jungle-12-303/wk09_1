/* ============================================================
 * alarm-negative.c — timer_sleep(음수) 엣지 케이스 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   timer_sleep(-100) 호출 시 크래시하지 않고 정상 반환.
 *   요구사항은 단 하나: 죽지 말 것.
 *
 * --- 왜 중요한가 ---
 *
 *   sleep_until = start + (-100) 같은 계산에서:
 *     - 정수 오버플로우 가능성
 *     - sleep_list 에 wakeup_tick 이 과거인 항목 → 즉시 깨어나야 함
 *     - 음수 ticks 를 timer_elapsed 와 비교할 때 부호 처리
 *
 *   busy-wait 구현 (현재) 은 while 조건 (timer_elapsed (start) < ticks) 에서
 *   ticks 가 -100 이면 0 < -100 이 false → 즉시 통과. 우연히 동작.
 *
 * --- Alarm Clock 과제 권장 ---
 *
 *   alarm-zero 와 함께 처리:
 *     if (ticks <= 0) return;
 *   양수만 sleep_list 에 넣고, 0 이하는 일찍 빠져나오기.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void
test_alarm_negative (void) 
{
  timer_sleep (-100);   /* 크래시 없이 즉시 반환되어야 함. */
  pass ();              /* 여기 도달하면 PASS. */
}
