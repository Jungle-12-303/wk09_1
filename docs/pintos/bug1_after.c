/* ============================================
 * 수정 코드 — Page Fault + EOI 문제 동시 해결
 *
 * 해결 1: list_empty 가드 → 빈 리스트 접근 방지
 * 해결 2: intr_context 분기 → 인터럽트 안에서는
 *         직접 yield 대신 플래그만 세팅
 * ============================================ */
#include "threads/interrupt.h"
#include "threads/thread.h"

extern struct list ready_list;

void check_preemption(void) {
  /* 해결 1: 빈 리스트면 바로 return */
  if (list_empty(&ready_list)) return;

  struct thread* front =
      list_entry(list_front(&ready_list), struct thread, elem);

  if (front->priority > thread_current()->priority) {
    /* 해결 2: 인터럽트 핸들러 안인가? */
    if (intr_context())
      /* Yes → 지금 yield하면 EOI 전에 스위치됨
       *        플래그만 세팅하고, intr_handler 복귀 후
       *        pic_end_of_interrupt() 다음에 yield */
      intr_yield_on_return();
    else
      /* No → 일반 컨텍스트, 바로 yield 가능 */
      thread_yield();
  }
}
