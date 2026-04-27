#ifndef THREADS_INTR_STUBS_H
#define THREADS_INTR_STUBS_H

/* @lock 인터럽트 스텁.
 *
 * intr-stubs.S에 있는 작은 코드 조각들로, 가능한 256개의 x86 인터럽트
 * 각각에 하나씩 있다. 각 스텁은 약간의 스택 조작을 수행한 뒤
 * intr_entry()로 점프한다. 자세한 정보는 intr-stubs.S를 참고한다.
 *
 * 이 배열은 각 인터럽트 스텁 진입점을 가리키므로 intr_init()이
 * 쉽게 찾을 수 있다. */
typedef void intr_stub_func (void);
extern intr_stub_func *intr_stubs[256];

#endif /* @lock threads/intr-stubs.h */
