/* ============================================================
 * tests.h — threads 프로젝트 테스트 함수 선언 모음
 *
 * tests.c 의 dispatch 테이블이 참조할 모든 테스트 함수를 선언한다.
 * 각 테스트의 실제 구현은 alarm-*.c, priority-*.c, mlfqs/*.c 에 있음.
 *
 * 또한 테스트가 공통으로 쓰는 출력 헬퍼 (msg/fail/pass) 도 선언.
 *
 * 테스트 그룹:
 *   alarm-*           : Alarm Clock (timer_sleep 동작 검증)
 *   priority-*        : Priority Scheduling (우선순위 기반 스케줄링)
 *   priority-donate-* : Priority Donation (우선순위 역전 해결)
 *   mlfqs-*           : Multi-Level Feedback Queue Scheduler
 * ============================================================ */
#ifndef TESTS_THREADS_TESTS_H
#define TESTS_THREADS_TESTS_H

void run_test (const char *);

typedef void test_func (void);

/* Alarm Clock 테스트들. */
extern test_func test_alarm_single;
extern test_func test_alarm_multiple;
extern test_func test_alarm_simultaneous;
extern test_func test_alarm_priority;
extern test_func test_alarm_zero;
extern test_func test_alarm_negative;

/* Priority Scheduling 테스트들. */
extern test_func test_priority_change;
extern test_func test_priority_donate_one;
extern test_func test_priority_donate_multiple;
extern test_func test_priority_donate_multiple2;
extern test_func test_priority_donate_sema;
extern test_func test_priority_donate_nest;
extern test_func test_priority_donate_lower;
extern test_func test_priority_donate_chain;
extern test_func test_priority_fifo;
extern test_func test_priority_preempt;
extern test_func test_priority_sema;
extern test_func test_priority_condvar;

/* MLFQS (Multi-Level Feedback Queue Scheduler) 테스트들. */
extern test_func test_mlfqs_load_1;
extern test_func test_mlfqs_load_60;
extern test_func test_mlfqs_load_avg;
extern test_func test_mlfqs_recent_1;
extern test_func test_mlfqs_fair_2;
extern test_func test_mlfqs_fair_20;
extern test_func test_mlfqs_nice_2;
extern test_func test_mlfqs_nice_10;
extern test_func test_mlfqs_block;

/* 테스트 출력 헬퍼.
   msg  : 일반 메시지를 "(test_name) ..." 형식으로 출력.
   fail : 실패 메시지 출력 후 커널 PANIC. 채점 스크립트가 매칭함.
   pass : "(test_name) PASS" 출력. 단순 테스트의 마지막에 호출. */
void msg (const char *, ...);
void fail (const char *, ...);
void pass (void);

#endif /* tests/threads/tests.h */
