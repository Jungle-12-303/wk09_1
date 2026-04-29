/* ============================================
 * 버그 코드 — 두 가지 문제를 동시에 가지고 있다
 *
 * 문제 1: 빈 ready_list에 접근 → Page Fault
 * 문제 2: 인터럽트 안에서 직접 yield → 시스템 멈춤
 *
 * thread_create() 끝에서 호출됨
 * ============================================ */
#include "threads/thread.h"

extern struct list ready_list;

void check_preemption(void) {
  /* 문제 1: ready_list가 비어있으면?
   *   list_front()가 sentinel 노드를 반환
   *   → list_entry()로 struct thread * 캐스팅
   *   → 유효하지 않은 메모리 → Page Fault!
   *
   * 부팅 시 thread_create("idle") 호출 시점에서
   * ready_list에는 아직 아무 스레드도 없다.
   */
  struct thread* front =
      list_entry(list_front(&ready_list), struct thread, elem);

  /* 문제 2: 여기가 인터럽트 핸들러 안에서 호출되면?
   *   thread_yield() → 컨텍스트 스위치 발생
   *   → 그런데 PIC에 EOI를 아직 안 보낸 상태
   *   → PIC: "이전 인터럽트 아직 처리 중"
   *   → 다음 타이머 IRQ0 영원히 차단
   *   → 시스템 멈춤!
   */
  if (front->priority > thread_current()->priority) thread_yield();
}
