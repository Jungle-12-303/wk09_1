/* ============================================
 * 버그 코드 — 우선순위가 안 내려가는 문제
 * ============================================ */
#include "threads/thread.h"

void thread_set_priority(int new_priority) {
  struct thread* curr = thread_current();

  /* 높아지는 경우만 반영 → 낮아지는 경우가 무시됨! */
  if (new_priority > curr->priority) curr->priority = new_priority;
}
