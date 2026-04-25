/* ============================================================
 * alarm-priority.c — alarm 으로 깨어난 스레드들이 우선순위 순으로 실행되는지 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   10 개 스레드가 모두 같은 시각에 깨어나도록 timer_sleep.
 *   각 스레드는 PRI_DEFAULT 보다 낮은 다양한 우선순위를 가짐.
 *
 *   같은 시각에 깨어났을 때, **우선순위가 높은 스레드부터 먼저 실행**
 *   되어야 한다 (= 메시지가 우선순위 내림차순으로 출력).
 *
 *   메인 스레드는 자기 우선순위를 PRI_MIN 으로 낮춰 sleeper 들이
 *   먼저 실행되도록 양보.
 *
 * --- 핵심 차이 (alarm-multiple 과의) ---
 *
 *   alarm-multiple    : 시각 정확성만 검증 (busy-wait 도 통과 가능)
 *   alarm-simultaneous: 동시 wakeup 검증
 *   alarm-priority    : 동시 wakeup + 우선순위 정렬 검증 ★
 *
 *   이 테스트는 Alarm Clock 만으로는 PASS 못 함.
 *   Priority Scheduling 이 같이 구현되어야 PASS.
 *   따라서 Project 1 의 두 단계 (Alarm + Priority) 모두 끝난 뒤 검증.
 *
 * --- 의존하는 기능 ---
 *
 *   1. timer_sleep 이 정확한 시각에 스레드를 깨움 (Alarm Clock).
 *   2. ready 큐가 우선순위 순 정렬 (Priority Scheduling).
 *   3. 더 높은 우선순위 스레드가 ready 가 되면 즉시 preempt
 *      (Priority Preemption).
 *
 * --- 우선순위 분포 ---
 *
 *   PRI_DEFAULT - (i + 5) % 10 - 1, i = 0..9
 *   → 30, 29, 28, ..., 21 (모두 PRI_DEFAULT 미만)
 *   메인은 PRI_MIN(0) 으로 낮춤.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func alarm_priority_thread;
static int64_t wake_time;       /* 모든 sleeper 가 깨어날 절대 시각. */
static struct semaphore wait_sema;  /* 메인이 sleeper 종료를 기다리는 데 사용. */

void
test_alarm_priority (void) 
{
  int i;
  
  /* MLFQS 에서는 동작 안 함 (priority 스케줄링이 다른 방식). */
  ASSERT (!thread_mlfqs);

  /* 모든 sleeper 가 깨어날 시각 = 지금부터 5 초 후. */
  wake_time = timer_ticks () + 5 * TIMER_FREQ;
  sema_init (&wait_sema, 0);
  
  /* 10 개의 sleeper 스레드 생성. 우선순위는 PRI_DEFAULT 미만의 다양한 값. */
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 5) % 10 - 1;
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, alarm_priority_thread, NULL);
    }

  /* 메인 스레드 우선순위를 최저로 낮춤.
     sleeper 들 (우선순위 21~30) 이 깨어났을 때 메인 (PRI_MIN=0) 보다
     무조건 우선되도록. */
  thread_set_priority (PRI_MIN);

  /* 10 개 sleeper 가 모두 sema_up 할 때까지 대기. */
  for (i = 0; i < 10; i++)
    sema_down (&wait_sema);
}

/* 각 sleeper 의 진입점.
 *
 * 흐름:
 *   1. tick 경계에 정렬 (busy-wait 으로 다음 tick 까지 기다림).
 *   2. wake_time 까지 timer_sleep.
 *   3. 깨어나면 자기 이름 출력 후 sema_up.
 *
 * 핵심:
 *   모든 sleeper 가 wake_time 동시에 깨어남.
 *   ready 큐에 동시에 진입한 뒤 우선순위 순으로 실행되어야 한다.
 *   따라서 출력은 우선순위 30, 29, 28, ... 순으로 나와야 PASS. */
static void
alarm_priority_thread (void *aux UNUSED) 
{
  /* 현재 tick 이 끝날 때까지 busy-wait → 다음 tick 의 시작 지점에 정렬.
     이렇게 해야 timer_sleep 호출과 timer interrupt 사이의 race 방지.
     (이 짧은 busy-wait 은 정확도를 위한 것, 우리가 고치는 timer_sleep 의
      busy-wait 과는 다른 의도) */
  int64_t start_time = timer_ticks ();
  while (timer_elapsed (start_time) == 0)
    continue;

  /* 이제 tick 의 정확한 시작 지점이므로 timer_sleep 안전.
     check time → timer interrupt 사이에 race 없음. */
  timer_sleep (wake_time - timer_ticks ());

  /* 깨어났을 때 메시지 출력.
     이 메시지의 순서가 우선순위 순으로 나와야 PASS. */
  msg ("Thread %s woke up.", thread_name ());

  sema_up (&wait_sema);
}
