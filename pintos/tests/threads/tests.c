/* ============================================================
 * tests.c — threads 프로젝트 테스트 dispatch
 *
 * 책임:
 *   1. "테스트 이름 (문자열)" → "테스트 함수 (포인터)" 매핑 테이블 보유.
 *   2. 커널 부팅 후 run_test(name) 호출 시, 테이블에서 찾아 실행.
 *   3. msg/fail/pass 같은 공통 출력 헬퍼 제공.
 *
 * 호출 흐름:
 *   pintos -- -q run alarm-multiple
 *     → 커널 init.c main() 마지막의 run_actions(argv)
 *       → run_task("alarm-multiple")
 *         → run_test("alarm-multiple")     ← 이 파일
 *           → tests[] 선형 탐색 → t->function() 호출
 *
 * 새 테스트 추가 시 해야 할 일:
 *   1. 새 .c 파일에 test_xxx 함수 작성.
 *   2. tests.h 에 extern 선언 추가.
 *   3. 본 파일의 tests[] 배열에 항목 추가.
 *   4. Make.tests 의 빌드 목록에 등록.
 *   5. 채점 스크립트 (.ck) 작성.
 * ============================================================ */
#include "tests/threads/tests.h"
#include <debug.h>
#include <string.h>
#include <stdio.h>

/* "이름 → 함수 포인터" 매핑 한 항목. */
struct test 
  {
    const char *name;          /* 커맨드라인에 입력하는 테스트 이름. */
    test_func *function;       /* 실행할 함수. */
  };

/* 등록된 모든 테스트의 dispatch 테이블.
   배열 순서는 의미 없음 — 이름으로 strcmp 매칭이라 무관. */
static const struct test tests[] = 
  {
    /* Alarm Clock — Project 1 첫 단계. */
    {"alarm-single", test_alarm_single},
    {"alarm-multiple", test_alarm_multiple},
    {"alarm-simultaneous", test_alarm_simultaneous},
    {"alarm-priority", test_alarm_priority},
    {"alarm-zero", test_alarm_zero},
    {"alarm-negative", test_alarm_negative},

    /* Priority Scheduling — Project 1 둘째 단계. */
    {"priority-change", test_priority_change},
    {"priority-donate-one", test_priority_donate_one},
    {"priority-donate-multiple", test_priority_donate_multiple},
    {"priority-donate-multiple2", test_priority_donate_multiple2},
    {"priority-donate-nest", test_priority_donate_nest},
    {"priority-donate-sema", test_priority_donate_sema},
    {"priority-donate-lower", test_priority_donate_lower},
    {"priority-donate-chain", test_priority_donate_chain},
    {"priority-fifo", test_priority_fifo},
    {"priority-preempt", test_priority_preempt},
    {"priority-sema", test_priority_sema},
    {"priority-condvar", test_priority_condvar},

    /* MLFQS — Project 1 마지막 단계 (선택적). */
    {"mlfqs-load-1", test_mlfqs_load_1},
    {"mlfqs-load-60", test_mlfqs_load_60},
    {"mlfqs-load-avg", test_mlfqs_load_avg},
    {"mlfqs-recent-1", test_mlfqs_recent_1},
    {"mlfqs-fair-2", test_mlfqs_fair_2},
    {"mlfqs-fair-20", test_mlfqs_fair_20},
    {"mlfqs-nice-2", test_mlfqs_nice_2},
    {"mlfqs-nice-10", test_mlfqs_nice_10},
    {"mlfqs-block", test_mlfqs_block},
  };

/* 현재 실행 중인 테스트의 이름. msg/fail/pass 가 출력 prefix 로 사용. */
static const char *test_name;

/* NAME 으로 지정된 테스트를 실행한다.
   tests[] 에서 strcmp 로 매칭되는 항목을 찾으면 그 함수를 호출.
   없으면 커널 PANIC.

   호출 전후로 "begin" / "end" 메시지를 찍어 채점 스크립트가
   테스트의 시작/끝을 인식할 수 있게 한다. */
void
run_test (const char *name) 
{
  const struct test *t;

  for (t = tests; t < tests + sizeof tests / sizeof *tests; t++)
    if (!strcmp (name, t->name))
      {
        test_name = name;
        msg ("begin");        /* 채점 매칭 — 변경 금지. */
        t->function ();       /* 실제 테스트 실행. */
        msg ("end");          /* 채점 매칭 — 변경 금지. */
        return;
      }
  PANIC ("no test named \"%s\"", name);
}

/* printf 처럼 FORMAT 을 출력하되, 앞에 "(test_name) " 을 붙이고
   끝에 줄바꿈을 자동 추가한다.
   채점 스크립트가 "(테스트이름) ..." 패턴으로 출력을 식별하므로
   이 형식은 절대 변경 금지. */
void
msg (const char *format, ...) 
{
  va_list args;
  
  printf ("(%s) ", test_name);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  putchar ('\n');
}

/* msg 와 동일하되 prefix 가 "(test_name) FAIL: " 이고
   출력 후 커널을 PANIC 시킨다.
   테스트 검증에 실패했을 때 호출. 채점 스크립트가 "FAIL" 로
   실패를 인식하므로 prefix 변경 금지. */
void
fail (const char *format, ...) 
{
  va_list args;
  
  printf ("(%s) FAIL: ", test_name);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  putchar ('\n');

  PANIC ("test failed");
}

/* "(test_name) PASS" 한 줄 출력.
   단순 테스트 (예: alarm-zero) 의 마지막에 호출하면 통과 표시.
   복잡한 테스트는 검증 로직이 직접 결과를 출력하고 pass() 안 쓰는 경우 많음. */
void
pass (void) 
{
  printf ("(%s) PASS\n", test_name);
}
