/* ============================================================
 * alarm-wait.c — alarm-single, alarm-multiple 테스트의 본체
 *
 * 한 파일이 두 개의 테스트 함수를 가진다 (PintOS 의 일반적 패턴):
 *   test_alarm_single   = test_sleep(5, 1)   → 5 스레드 × 1 회 sleep
 *   test_alarm_multiple = test_sleep(5, 7)   → 5 스레드 × 7 회 sleep
 *
 * --- 무엇을 검증하는가 ---
 *
 *   N 개의 스레드가 각자 다른 duration 으로 M 회 sleep 한다.
 *   스레드 i 는 매번 (i+1)*10 틱을 잠든다.
 *     thread 0: 매 sleep 10 틱
 *     thread 1: 매 sleep 20 틱
 *     thread 2: 매 sleep 30 틱
 *     ...
 *
 *   각 스레드가 깨어나는 순간을 output 배열에 기록.
 *   모든 스레드가 끝난 뒤 깨어난 순서를 검사:
 *   (iteration * duration) 값이 nondescending (단조 증가) 여야 PASS.
 *
 * --- 왜 그래야 하는가 ---
 *
 *   thread A 가 N 틱 후 깨어나기로 했고
 *   thread B 가 M 틱 후 깨어나기로 했다면 (N < M),
 *   A 가 반드시 B 보다 먼저 깨어나야 한다.
 *   sleep 구현이 시각을 정확히 지킨다면 자동으로 만족.
 *
 * --- Alarm Clock 과제 관점 ---
 *
 *   현재 timer_sleep 은 busy-wait (while + thread_yield) 구조.
 *   이 테스트는 "결과적 wakeup 순서" 만 보므로 busy-wait 도 PASS 가능.
 *   다만 CPU 100% 점유와 비효율은 별개 (이 테스트로는 안 잡힘).
 *
 *   과제의 목표는 timer_sleep 을 thread_block() 기반으로 바꾸기:
 *     - 스레드가 정확한 시각에 깨어나는지 (이 테스트가 검증)
 *     - CPU 가 idle 로 들어갈 수 있는지 (눈으로 확인)
 *
 * --- 채점 스크립트 ---
 *
 *   tests/threads/alarm-wait.ck (Perl) 가 stdout 의 "thread X: ..." 줄을
 *   파싱해서 product 가 단조 증가인지 확인.
 *   msg() 의 문자열 인자는 절대 변경 금지.
 *
 * --- 핵심 데이터 구조 ---
 *
 *   sleep_test     : 모든 스레드가 공유하는 정보 (시작 시각, 출력 버퍼)
 *   sleep_thread   : 각 스레드의 개별 정보 (id, duration, iteration count)
 * ============================================================ */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void test_sleep (int thread_cnt, int iterations);

/* alarm-single: 5 개 스레드가 각자 1 회 sleep. 가장 단순한 시나리오. */
void
test_alarm_single (void) 
{
  test_sleep (5, 1);
}

/* alarm-multiple: 5 개 스레드가 각자 7 회 sleep. 동시 sleep 부하 검증. */
void
test_alarm_multiple (void) 
{
  test_sleep (5, 7);
}

/* 모든 sleep 스레드가 공유하는 테스트 컨텍스트. */
struct sleep_test 
  {
    int64_t start;              /* 테스트 시작 시각 (틱 단위). */
    int iterations;             /* 각 스레드가 sleep 할 횟수. */

    /* 출력 버퍼. 여러 스레드가 동시에 기록하므로 락이 필요하다. */
    struct lock output_lock;    /* 출력 버퍼 보호 락. */
    int *output_pos;            /* 다음 기록 위치. id 를 적는다. */
  };

/* 개별 sleep 스레드의 상태. */
struct sleep_thread 
  {
    struct sleep_test *test;     /* 모든 스레드가 공유하는 컨텍스트. */
    int id;                     /* 스레드 번호 (0 ~ N-1). */
    int duration;               /* 매 sleep 의 틱 수 = (id+1)*10. */
    int iterations;             /* 지금까지 깨어난 횟수. */
  };

static void sleeper (void *);

/* THREAD_CNT 개 스레드를 만들어 각각 ITERATIONS 회 sleep 시킨다.
 *
 * 흐름:
 *   1. 메인 스레드가 sleep_test 와 N 개의 sleep_thread 를 준비.
 *   2. 각 스레드를 thread_create 로 띄움 → sleeper 진입점.
 *   3. 메인은 모든 스레드가 끝날 시간 + 여유를 두고 timer_sleep.
 *   4. 메인이 깨어나면 출력 버퍼를 순회하며 wakeup 순서를 검증. */
static void
test_sleep (int thread_cnt, int iterations) 
{
  struct sleep_test test;
  struct sleep_thread *threads;
  int *output, *op;
  int product;
  int i;

  /* MLFQS 모드에서는 동작 안 함 (priority 기반 검증 방식이 다르다). */
  ASSERT (!thread_mlfqs);

  /* 채점 스크립트가 매칭하는 출력 — 문자열 변경 금지. */
  msg ("Creating %d threads to sleep %d times each.", thread_cnt, iterations);
  msg ("Thread 0 sleeps 10 ticks each time,");
  msg ("thread 1 sleeps 20 ticks each time, and so on.");
  msg ("If successful, product of iteration count and");
  msg ("sleep duration will appear in nondescending order.");

  /* 메모리 할당.
     threads: N 개 sleep_thread 구조체.
     output : N*M*2 개 int (각 wakeup 마다 id 1 개 기록 + 여유분). */
  threads = malloc (sizeof *threads * thread_cnt);
  output = malloc (sizeof *output * iterations * thread_cnt * 2);
  if (threads == NULL || output == NULL)
    PANIC ("couldn't allocate memory for test");

  /* 테스트 컨텍스트 초기화.
     start = 현재시각 + 100. 100 틱 여유를 둬서
     아래 thread_create 들이 모두 시작할 시간을 벌어둔다. */
  test.start = timer_ticks () + 100;
  test.iterations = iterations;
  lock_init (&test.output_lock);
  test.output_pos = output;

  /* N 개 sleep 스레드 생성.
     각 스레드의 duration 은 (id+1)*10 틱.
     sleeper(t) 함수가 진입점, t 는 sleep_thread* 인자. */
  ASSERT (output != NULL);
  for (i = 0; i < thread_cnt; i++)
    {
      struct sleep_thread *t = threads + i;
      char name[16];
      
      t->test = &test;
      t->id = i;
      t->duration = (i + 1) * 10;
      t->iterations = 0;

      snprintf (name, sizeof name, "thread %d", i);
      thread_create (name, PRI_DEFAULT, sleeper, t);
    }
  
  /* 모든 스레드가 끝날 때까지 메인이 대기.
     가장 오래 자는 스레드 = thread_cnt 번 × iterations 회 × 10 틱.
     시작 여유 100 + 종료 여유 100 추가. */
  timer_sleep (100 + thread_cnt * iterations * 10 + 100);

  /* 혹시 좀비 스레드가 아직 남아 출력하지 못하도록 락을 획득해 둔다. */
  lock_acquire (&test.output_lock);

  /* 깨어난 순서 검증 — 핵심.
     output[] 에는 깨어난 순서대로 thread id 가 기록돼 있다.
     각 기록을 순회하며 (iteration * duration) 이 단조 증가인지 확인.
     단조 증가면 sleep 시각이 정확히 지켜진 것.
     한 번이라도 감소하면 wakeup 순서가 뒤바뀜 → FAIL. */
  product = 0;
  for (op = output; op < test.output_pos; op++) 
    {
      struct sleep_thread *t;
      int new_prod;

      ASSERT (*op >= 0 && *op < thread_cnt);
      t = threads + *op;

      new_prod = ++t->iterations * t->duration;
        
      /* 채점 대상 출력 — 문자열 변경 금지. */
      msg ("thread %d: duration=%d, iteration=%d, product=%d",
           t->id, t->duration, t->iterations, new_prod);
      
      if (new_prod >= product)
        product = new_prod;
      else
        /* 채점 대상 출력 — 문자열 변경 금지. */
        fail ("thread %d woke up out of order (%d > %d)!",
              t->id, product, new_prod);
    }

  /* 각 스레드가 정확히 iterations 번 깨어났는지 확인.
     깨어난 횟수가 모자라면 timer_sleep 이 늦거나 멈춘 것. */
  for (i = 0; i < thread_cnt; i++)
    if (threads[i].iterations != iterations)
      fail ("thread %d woke up %d times instead of %d",
            i, threads[i].iterations, iterations);
  
  lock_release (&test.output_lock);
  free (output);
  free (threads);
}

/* 개별 sleep 스레드의 진입점.
 *
 * 흐름:
 *   1. 다음 깨어날 절대 시각 계산 = start + i * duration
 *   2. 그 시각까지 timer_sleep (지금부터 그때까지의 차이만큼)
 *   3. 깨어나면 output 버퍼에 자기 id 기록
 *   4. iterations 만큼 반복
 *
 * 핵심: timer_sleep 이 정확히 sleep_until 시각에 깨워야 함.
 *       만약 늦게 깨우면 다른 스레드보다 늦게 기록 → 순서 어긋남 → FAIL. */
static void
sleeper (void *t_) 
{
  struct sleep_thread *t = t_;
  struct sleep_test *test = t->test;
  int i;

  for (i = 1; i <= test->iterations; i++) 
    {
      int64_t sleep_until = test->start + i * t->duration;
      timer_sleep (sleep_until - timer_ticks ());
      lock_acquire (&test->output_lock);
      *test->output_pos++ = t->id;
      lock_release (&test->output_lock);
    }
}
