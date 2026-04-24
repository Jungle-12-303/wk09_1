#ifndef THREADS_INTR_STUBS_H
#define THREADS_INTR_STUBS_H

/* 인터럽트 스텁 (Interrupt Stubs).
 *
 * intr-stubs.S에 정의된 작은 코드 조각들로,
 * x86에서 가능한 256개 인터럽트 각각에 하나씩 있다.
 * 각 스텁은 약간의 스택 조작을 한 뒤 intr_entry()로 점프한다.
 * 자세한 내용은 intr-stubs.S 참고.
 *
 * 이 배열은 각 인터럽트 스텁의 진입점을 가리키며,
 * intr_init()에서 IDT를 구성할 때 이 배열을 사용한다. */
typedef void intr_stub_func (void);
extern intr_stub_func *intr_stubs[256];

#endif /* threads/intr-stubs.h */
