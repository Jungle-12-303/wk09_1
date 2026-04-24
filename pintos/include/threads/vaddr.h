#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* 가상 주소 관련 함수/매크로.
 *
 * x86 하드웨어 페이지 테이블 전용 함수/매크로는 pte.h 참고. */

/* 비트 마스크 생성: SHIFT 위치부터 CNT개 비트를 1로 설정. */
#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* 페이지 오프셋 관련 상수 (비트 0~11).
 * 4KB 페이지: 하위 12비트가 페이지 내 오프셋. */
#define PGSHIFT 0                          /* 오프셋 시작 비트 위치. */
#define PGBITS  12                         /* 오프셋 비트 수. */
#define PGSIZE  (1 << PGBITS)              /* 페이지 크기 = 4096 바이트 = 4KB. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* 페이지 오프셋 마스크 (0xFFF). */

/* 가상 주소에서 페이지 내 오프셋 추출 (0~4095). */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)

/* 가상 주소에서 페이지 번호 추출 (상위 비트). */
#define pg_no(va) ((uint64_t) (va) >> PGBITS)

/* 가장 가까운 페이지 경계로 올림. */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK))

/* 가장 가까운 페이지 경계로 내림.
 * running_thread()에서 RSP로부터 struct thread 주소를 역산할 때 사용. */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK)

/* 커널 가상 주소 시작점.
 * 이 주소 이상 = 커널 영역, 미만 = 유저 영역. */
#define KERN_BASE LOADER_KERN_BASE

/* 유저 스택 시작 주소 (유저 프로세스용). */
#define USER_STACK 0x47480000

/* VADDR이 유저 가상 주소인지 확인. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* VADDR이 커널 가상 주소인지 확인. */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: add checking
/* 물리 주소 → 커널 가상 주소 변환.
 * 물리 주소에 KERN_BASE를 더하면 커널이 접근 가능한 가상 주소가 된다. */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))

/* 커널 가상 주소 → 물리 주소 변환.
 * 커널 가상 주소에서 KERN_BASE를 빼면 물리 주소가 된다.
 * 반드시 커널 주소여야 하므로 ASSERT로 검증. */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* threads/vaddr.h */
