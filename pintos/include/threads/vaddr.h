#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* @lock 가상 주소를 다루기 위한 함수와 매크로.
 *
 * x86 하드웨어 페이지 테이블 전용 함수와 매크로는 pte.h를 참고한다. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* @lock 페이지 오프셋(비트 0:12). */
#define PGSHIFT 0                          /* @lock 첫 번째 오프셋 비트의 인덱스. */
#define PGBITS  12                         /* @lock 오프셋 비트 수. */
#define PGSIZE  (1 << PGBITS)              /* @lock 페이지 하나의 바이트 수. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* @lock 페이지 오프셋 비트(0:12). */

/* @lock 페이지 내부 오프셋. */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)

#define pg_no(va) ((uint64_t) (va) >> PGBITS)

/* @lock 가장 가까운 페이지 경계로 올림한다. */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK))

/* @lock 가장 가까운 페이지 경계로 내림한다. */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK)

/* @lock 커널 가상 주소 시작점. */
#define KERN_BASE LOADER_KERN_BASE

/* @lock 사용자 스택 시작점. */
#define USER_STACK 0x47480000

/* @lock VADDR이 사용자 가상 주소이면 true를 반환한다. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* @lock VADDR이 커널 가상 주소이면 true를 반환한다. */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// @lock FIXME: 검사 추가
/* @lock 물리 주소 PADDR이 매핑된 커널 가상 주소를 반환한다. */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))

/* @lock 커널 가상 주소 VADDR이 매핑된 물리 주소를 반환한다. */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* @lock threads/vaddr.h */
