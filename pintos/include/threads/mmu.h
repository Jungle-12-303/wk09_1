#ifndef THREAD_MMU_H
#define THREAD_MMU_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/pte.h"

/* 페이지 테이블 순회 콜백 함수 타입.
 * pml4_for_each()가 각 PTE에 대해 호출한다.
 *   pte : 페이지 테이블 엔트리 포인터
 *   va  : 해당 PTE가 매핑하는 가상 주소
 *   aux : 사용자 데이터
 * false를 반환하면 순회 중단. */
typedef bool pte_for_each_func (uint64_t *pte, void *va, void *aux);

/* 4단계 페이지 테이블 (PML4) 관리 함수. */
uint64_t *pml4e_walk (uint64_t *pml4, const uint64_t va, int create);
	/* va에 해당하는 PTE를 찾아 반환. create=1이면 중간 테이블 자동 생성. */
uint64_t *pml4_create (void);
	/* 새 PML4 테이블 생성 (palloc으로 페이지 할당). */
bool pml4_for_each (uint64_t *, pte_for_each_func *, void *);
	/* 모든 PTE에 대해 콜백 실행. */
void pml4_destroy (uint64_t *pml4);
	/* PML4와 하위 모든 페이지 테이블 해제. */
void pml4_activate (uint64_t *pml4);
	/* CR3 레지스터에 PML4 주소를 로드하여 활성화. */
void *pml4_get_page (uint64_t *pml4, const void *upage);
	/* upage(유저 가상 주소)에 매핑된 커널 가상 주소 반환. */
bool pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw);
	/* upage를 kpage에 매핑. rw=true이면 읽기/쓰기, false이면 읽기 전용. */
void pml4_clear_page (uint64_t *pml4, void *upage);
	/* upage의 매핑 제거 (PTE를 미존재로 설정). */
bool pml4_is_dirty (uint64_t *pml4, const void *upage);
	/* upage에 쓰기가 발생했는지 (dirty 비트) 확인. */
void pml4_set_dirty (uint64_t *pml4, const void *upage, bool dirty);
	/* upage의 dirty 비트를 수동 설정/해제. */
bool pml4_is_accessed (uint64_t *pml4, const void *upage);
	/* upage에 접근이 있었는지 (accessed 비트) 확인. */
void pml4_set_accessed (uint64_t *pml4, const void *upage, bool accessed);
	/* upage의 accessed 비트를 수동 설정/해제. */

/* PTE 플래그 검사 매크로. */
#define is_writable(pte) (*(pte) & PTE_W)      /* 쓰기 가능한지. */
#define is_user_pte(pte) (*(pte) & PTE_U)      /* 유저 모드 접근 가능한지. */
#define is_kern_pte(pte) (!is_user_pte (pte))  /* 커널 전용인지. */

/* PTE에서 물리 주소 추출 (하위 12비트 플래그 제거). */
#define pte_get_paddr(pte) (pg_round_down(*(pte)))

/* x86-64 세그먼트 디스크립터 포인터.
 * lgdt/lidt 명령어에 전달하여 GDT/IDT 위치를 CPU에 알린다.
 * packed: CPU가 요구하는 정확한 바이트 레이아웃 보장. */
struct desc_ptr {
	uint16_t size;       /* 테이블 크기 - 1 (바이트). */
	uint64_t address;    /* 테이블 시작 물리/가상 주소. */
} __attribute__((packed));

#endif /* thread/mm.h */
