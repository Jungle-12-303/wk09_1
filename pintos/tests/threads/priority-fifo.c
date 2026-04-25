/* ============================================================
 * priority-fifo.c — 같은 우선순위 스레드들의 라운드로빈 순서 일관성 검증
 *
 * --- 무엇을 검증하는가 ---
 *
 *   같은 우선순위의 스레드 16 개가 16 번 반복 실행할 때, 매 iteration 의
 *   실행 순서가 동일해야 한다 (= 라운드로빈, FIFO 순서).
 *
 *   시나리오:
 *     1. 메인이 자기 우선순위를 PRI_DEFAULT+2 로 올림.
 *     2. PRI_DEFAULT+1 우선순위 스레드 16 개 생성.
 *        메인이 더 높아서 thread_create 들이 모두 끝까지 진행됨.
 *     3. 메인이 자기 우선순위를 PRI_DEFAULT 로 낮춤.
 *        16 개 스레드 (PRI_DEFAULT+1) 가 메인보다 높아져 모두 실행됨.
 *     4. 각 스레드는 16 회 반복: lock 획득 → 자기 id 출력 → lock 해제 → yield.
 *     5. 출력 순서가 매 iteration 마다 같아야 함 (= 0 1 2 ... 15 같은 식).
 *
 * --- 핵심 검증 포인트 ---
 *
 *   ready 큐에서 같은 우선순위의 스레드가 여러 개 있을 때 FIFO 순서로
 *   처리되어야 한다 (LIFO 나 임의 순서가 되면 출력 패턴이 깨짐).
 *
 * --- 구현 힌트 ---
 *
 *   list_insert_ordered 사용 시 동일 우선순위면 뒤에 삽입 (FIFO).
 *   list_max 로 꺼낼 때 동률이면 첫 번째 것을 선택.
 *
 * --- 출처 ---
 *
 *   Stanford CS 140 (1999, Matt Franklin / Greg Hutchins / Yu Ping Hu).
 *   arens 가 수정.
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 각 sleeper 의 상태. */
struct simple_thread_data 
  {
    int id;                     /* sleeper 번호 (0~15). */
    int iterations;             /* 지금까지 반복 횟수. */
    struct lock *lock;          /* 출력 버퍼 보호 락. */
    int **op;                   /* 출력 위치 포인터의 포인터. */
  };

#define THREAD_CNT 16   /* 스레드 수. */
#define ITER_CNT 16     /* 각 스레드의 반복 횟수. */

static thread_func simple_thread_func;

void
test_priority_fifo (void) 
{
  struct simple_thread_data data[THREAD_CNT];
  struct lock lock;
  int *output, *op;
  int i, cnt;

  /* MLFQS 에서는 동작 안 함. */
  ASSERT (!thread_mlfqs);

  /* 메인 우선순위가 기본값인지 확인. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  msg ("%d threads will iterate %d times in the same order each time.",
       THREAD_CNT, ITER_CNT);
  msg ("If the order varies then there is a bug.");

  /* 출력 버퍼 (각 iteration 의 id 기록용) 와 락 초기화. */
  output = op = malloc (sizeof *output * THREAD_CNT * ITER_CNT * 2);
  ASSERT (output != NULL);
  lock_init (&lock);

  /* 메인을 잠시 PRI_DEFAULT+2 로 올림 → thread_create 들이 끝까지 진행. */
  thread_set_priority (PRI_DEFAULT + 2);
  for (i = 0; i < THREAD_CNT; i++) 
    {
      char name[16];
      struct simple_thread_data *d = data + i;
      snprintf (name, sizeof name, "%d", i);
      d->id = i;
      d->iterations = 0;
      d->lock = &lock;
      d->op = &op;
      thread_create (name, PRI_DEFAULT + 1, simple_thread_func, d);
    }

  /* 메인 우선순위를 다시 PRI_DEFAULT 로 낮춤 → 16 개 스레드가 더 높아져
     이 시점부터 라운드로빈으로 실행됨. */
  thread_set_priority (PRI_DEFAULT);
  /* 모든 스레드가 끝까지 실행됨. 락이 free 라야 정상. */
  ASSERT (lock.holder == NULL);

  /* output[] 에 기록된 순서대로 id 출력.
     매 16 개씩 묶어 한 줄로 — iteration 단위 패턴이 보임. */
  cnt = 0;
  for (; output < op; output++) 
    {
      struct simple_thread_data *d;

      ASSERT (*output >= 0 && *output < THREAD_CNT);
      d = data + *output;
      if (cnt % THREAD_CNT == 0)
        printf ("(priority-fifo) iteration:");
      printf (" %d", d->id);
      if (++cnt % THREAD_CNT == 0)
        printf ("\n");
      d->iterations++;
    }
}

/* 각 sleeper 의 진입점.
 *
 * 16 회 반복하며:
 *   1. lock 획득 (출력 버퍼 보호)
 *   2. 자기 id 를 출력 버퍼에 기록
 *   3. lock 해제
 *   4. thread_yield() 로 다음 스레드에 양보
 *
 * 모두 같은 우선순위라 ready 큐의 FIFO 순서대로 깨어나야 함. */
static void 
simple_thread_func (void *data_) 
{
  struct simple_thread_data *data = data_;
  int i;
  
  for (i = 0; i < ITER_CNT; i++) 
    {
      lock_acquire (data->lock);
      *(*data->op)++ = data->id;
      lock_release (data->lock);
      thread_yield ();
    }
}
