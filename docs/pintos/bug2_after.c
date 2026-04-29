/* ============================================
 * 수정 코드 — origin_priority + refresh_priority
 * ============================================ */
#include "threads/thread.h"

extern struct list ready_list;

void check_preemption(void);
void refresh_priority(struct thread* t);

void thread_set_priority(int new_priority) {
  struct thread* curr = thread_current();
  curr->origin_priority = new_priority; /* 원래 우선순위 저장 */
  refresh_priority(curr);               /* 기부 고려하여 재계산 */
  check_preemption();                   /* 선점 여부 확인 */
}

void refresh_priority(struct thread* t) {
  struct list_elem* e;

  t->priority = t->origin_priority; /* 원래 값에서 시작 */

  for (e = list_begin(&t->donations); e != list_end(&t->donations);
       e = list_next(e)) {
    struct thread* donor = list_entry(e, struct thread, donation_elem);
    if (donor->priority > t->priority)
      t->priority = donor->priority; /* 기부자 중 최댓값 적용 */
  }
}
