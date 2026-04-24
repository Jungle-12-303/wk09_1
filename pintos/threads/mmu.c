/*
 * ==========================================================================
 *  x86-64 4단계 페이지 테이블(PML4) 관리 모듈
 * ==========================================================================
 *
 *  x86-64 아키텍처는 4단계 페이지 테이블 구조를 사용하여 가상 주소를
 *  물리 주소로 변환한다. 각 단계는 다음과 같다:
 *
 *    PML4 (Page Map Level 4)
 *      -> PDPT (Page Directory Pointer Table)
 *        -> PD (Page Directory)
 *          -> PT (Page Table)
 *            -> 물리 페이지 프레임
 *
 *  48비트 가상 주소의 비트 분해:
 *  +-----------+-----------+-----------+-----------+------------+
 *  | PML4 인덱스 | PDPE 인덱스 | PDX 인덱스  | PTX 인덱스  |  오프셋     |
 *  | [47:39]   | [38:30]   | [29:21]   | [20:12]   | [11:0]     |
 *  |  9비트     |  9비트     |  9비트     |  9비트     | 12비트      |
 *  +-----------+-----------+-----------+-----------+------------+
 *
 *  각 테이블은 512개(2^9)의 엔트리를 가지며, 한 페이지(4KB)를 차지한다.
 *  각 엔트리는 8바이트(uint64_t)이므로 512 * 8 = 4096 = PGSIZE.
 *
 *  페이지 테이블 엔트리(PTE)의 주요 플래그 비트:
 *    PTE_P (Present)  - 해당 페이지가 메모리에 존재함을 표시.
 *                       이 비트가 0이면 해당 주소 접근 시 페이지 폴트 발생.
 *    PTE_W (Writable) - 쓰기 가능 여부. 0이면 읽기 전용.
 *    PTE_U (User)     - 사용자 모드 접근 허용 여부.
 *                       0이면 커널 모드에서만 접근 가능.
 *    PTE_D (Dirty)    - CPU가 해당 페이지에 쓰기를 수행하면 하드웨어가
 *                       자동으로 1로 설정. 페이지가 수정되었는지 확인할 때 사용.
 *    PTE_A (Accessed) - CPU가 해당 페이지를 읽거나 쓰면 하드웨어가
 *                       자동으로 1로 설정. 페이지 교체 알고리즘에서 사용.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "intrinsic.h"

/*
 * pgdir_walk - 3단계(Page Directory) 워크 함수
 *
 * 4단계 페이지 테이블 워크 중 세 번째 단계를 담당한다.
 * Page Directory(PD)에서 가상 주소 va에 해당하는 Page Table(PT) 엔트리의
 * 커널 가상 주소를 반환한다.
 *
 * 매개변수:
 *   pdp    - Page Directory의 커널 가상 주소 (이 함수의 "pdp"는 실제로 PD를 가리킴)
 *   va     - 변환할 가상 주소
 *   create - 1이면 해당 엔트리가 없을 때 새 Page Table을 할당하여 생성.
 *            0이면 엔트리가 없을 때 NULL을 반환.
 *
 * 동작 과정:
 *   1. va에서 PDX (Page Directory Index, 비트 [29:21])를 추출한다.
 *   2. PD[PDX]에 해당하는 엔트리를 확인한다.
 *   3. PTE_P(Present) 비트가 설정되어 있지 않으면:
 *      - create가 1이면 새 페이지를 할당하고 PTE_U|PTE_W|PTE_P 플래그와 함께 등록.
 *      - create가 0이면 NULL 반환.
 *   4. 최종적으로 PT 내에서 PTX (비트 [20:12]) 인덱스에 해당하는 엔트리의 주소를 반환.
 *
 * 반환값: PT 엔트리의 커널 가상 주소. 실패 시 NULL.
 */
static uint64_t *
pgdir_walk (uint64_t *pdp, const uint64_t va, int create) {
	/* va에서 Page Directory 인덱스(비트 [29:21])를 추출 */
	int idx = PDX (va);
	if (pdp) {
		/* PD의 해당 엔트리를 가져옴 */
		uint64_t *pte = (uint64_t *) pdp[idx];
		if (!((uint64_t) pte & PTE_P)) {
			/* 엔트리가 존재하지 않음 (Present 비트가 0) */
			if (create) {
				/* 새 Page Table 페이지를 0으로 초기화하여 할당 */
				uint64_t *new_page = palloc_get_page (PAL_ZERO);
				if (new_page)
					/* 물리 주소로 변환 후 사용자/쓰기/존재 플래그를 설정하여 등록 */
					pdp[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;
				else
					return NULL;
			} else
				return NULL;
		}
		/*
		 * PD 엔트리에서 물리 주소를 추출하고(PTE_ADDR),
		 * 커널 가상 주소로 변환한 뒤(ptov),
		 * PTX 인덱스(비트 [20:12])에 해당하는 오프셋(8 * PTX)을 더해
		 * 최종 PT 엔트리의 주소를 반환한다.
		 */
		return (uint64_t *) ptov (PTE_ADDR (pdp[idx]) + 8 * PTX (va));
	}
	return NULL;
}

/*
 * pdpe_walk - 2단계(Page Directory Pointer Table) 워크 함수
 *
 * 4단계 페이지 테이블 워크 중 두 번째 단계를 담당한다.
 * PDPT에서 가상 주소 va에 해당하는 PD 엔트리를 찾고,
 * 이어서 pgdir_walk를 호출하여 최종 PT 엔트리까지 도달한다.
 *
 * 매개변수:
 *   pdpe   - PDPT(Page Directory Pointer Table)의 커널 가상 주소
 *   va     - 변환할 가상 주소
 *   create - 1이면 중간 테이블이 없을 때 새로 할당.
 *            0이면 없을 때 NULL 반환.
 *
 * 동작 과정:
 *   1. va에서 PDPE 인덱스(비트 [38:30])를 추출한다.
 *   2. PDPT[PDPE]의 Present 비트를 확인한다.
 *   3. 비어 있고 create가 1이면 새 Page Directory 페이지를 할당한다.
 *   4. pgdir_walk를 호출하여 나머지 단계를 수행한다.
 *   5. 하위 워크가 실패하고 이번 호출에서 새 페이지를 할당했다면,
 *      할당한 페이지를 해제하고 엔트리를 초기화한다 (롤백).
 *
 * 반환값: 최종 PT 엔트리의 커널 가상 주소. 실패 시 NULL.
 */
static uint64_t *
pdpe_walk (uint64_t *pdpe, const uint64_t va, int create) {
	uint64_t *pte = NULL;
	/* va에서 PDPE 인덱스(비트 [38:30])를 추출 */
	int idx = PDPE (va);
	/* 이번 호출에서 새 페이지를 할당했는지 추적하는 플래그 */
	int allocated = 0;
	if (pdpe) {
		/* PDPT의 해당 엔트리를 가져옴 */
		uint64_t *pde = (uint64_t *) pdpe[idx];
		if (!((uint64_t) pde & PTE_P)) {
			/* 엔트리가 존재하지 않음 (Present 비트가 0) */
			if (create) {
				/* 새 Page Directory 페이지를 0으로 초기화하여 할당 */
				uint64_t *new_page = palloc_get_page (PAL_ZERO);
				if (new_page) {
					/* 물리 주소로 변환 후 플래그를 설정하여 등록 */
					pdpe[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;
					allocated = 1;
				} else
					return NULL;
			} else
				return NULL;
		}
		/* 다음 단계(Page Directory)로 워크를 계속한다 */
		pte = pgdir_walk (ptov (PTE_ADDR (pdpe[idx])), va, create);
	}
	/*
	 * 롤백 처리: 하위 단계 워크가 실패(pte == NULL)했는데
	 * 이번 호출에서 새 페이지를 할당했다면, 해당 페이지를 해제하고
	 * PDPT 엔트리를 0으로 초기화하여 일관성을 유지한다.
	 */
	if (pte == NULL && allocated) {
		palloc_free_page ((void *) ptov (PTE_ADDR (pdpe[idx])));
		pdpe[idx] = 0;
	}
	return pte;
}

/*
 * pml4e_walk - 1단계(PML4) 워크 함수 (페이지 테이블 워크의 진입점)
 *
 * 4단계 페이지 테이블 워크의 최상위 단계를 담당한다.
 * PML4 테이블에서 시작하여 가상 주소 va에 대응하는 최종 PT 엔트리를
 * 찾아 그 커널 가상 주소를 반환한다.
 *
 * 워크 경로: PML4 -> PDPT -> PD -> PT
 *
 * 매개변수:
 *   pml4e  - PML4(Page Map Level 4) 테이블의 커널 가상 주소
 *   va     - 변환할 가상 주소
 *   create - 1이면 중간 테이블이 없을 때 새로 할당하여 생성.
 *            0이면 없을 때 NULL 반환.
 *
 * 동작 과정:
 *   1. va에서 PML4 인덱스(비트 [47:39])를 추출한다.
 *   2. PML4[idx]의 Present 비트를 확인한다.
 *   3. 비어 있고 create가 1이면 새 PDPT 페이지를 할당한다.
 *   4. pdpe_walk를 호출하여 나머지 단계를 수행한다.
 *   5. 하위 워크가 실패하고 이번 호출에서 새 페이지를 할당했다면 롤백한다.
 *
 * 반환값: 최종 PT 엔트리의 커널 가상 주소. 실패 시 NULL.
 */
uint64_t *
pml4e_walk (uint64_t *pml4e, const uint64_t va, int create) {
	uint64_t *pte = NULL;
	/* va에서 PML4 인덱스(비트 [47:39])를 추출 */
	int idx = PML4 (va);
	/* 이번 호출에서 새 페이지를 할당했는지 추적하는 플래그 */
	int allocated = 0;
	if (pml4e) {
		/* PML4의 해당 엔트리를 가져옴 */
		uint64_t *pdpe = (uint64_t *) pml4e[idx];
		if (!((uint64_t) pdpe & PTE_P)) {
			/* 엔트리가 존재하지 않음 (Present 비트가 0) */
			if (create) {
				/* 새 PDPT 페이지를 0으로 초기화하여 할당 */
				uint64_t *new_page = palloc_get_page (PAL_ZERO);
				if (new_page) {
					/* 물리 주소로 변환 후 플래그를 설정하여 등록 */
					pml4e[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;
					allocated = 1;
				} else
					return NULL;
			} else
				return NULL;
		}
		/* 다음 단계(PDPT)로 워크를 계속한다 */
		pte = pdpe_walk (ptov (PTE_ADDR (pml4e[idx])), va, create);
	}
	/*
	 * 롤백 처리: 하위 단계 워크가 실패(pte == NULL)했는데
	 * 이번 호출에서 새 페이지를 할당했다면, 해당 페이지를 해제하고
	 * PML4 엔트리를 0으로 초기화하여 일관성을 유지한다.
	 */
	if (pte == NULL && allocated) {
		palloc_free_page ((void *) ptov (PTE_ADDR (pml4e[idx])));
		pml4e[idx] = 0;
	}
	return pte;
}

/*
 * pml4_create - 새로운 PML4 페이지 테이블을 생성한다.
 *
 * 새 프로세스를 위한 최상위 페이지 테이블(PML4)을 할당하고,
 * 커널 영역의 매핑을 복사한다. 사용자 영역 매핑은 비어 있는 상태로 시작.
 *
 * 커널 가상 주소 공간은 모든 프로세스가 공유하므로, 부팅 시 생성된
 * base_pml4의 내용을 그대로 복사한다. 이렇게 하면 커널 코드와 데이터에
 * 어떤 프로세스에서든 동일한 가상 주소로 접근할 수 있다.
 *
 * 반환값: 새로 생성된 PML4의 커널 가상 주소. 메모리 부족 시 NULL.
 */
uint64_t *
pml4_create (void) {
	uint64_t *pml4 = palloc_get_page (0);
	if (pml4)
		/* base_pml4(커널 페이지 테이블)의 전체 내용을 복사.
		 * 이를 통해 커널 영역 매핑이 새 주소 공간에도 유지된다. */
		memcpy (pml4, base_pml4, PGSIZE);
	return pml4;
}

/*
 * pt_for_each - Page Table(4단계, 최하위) 순회 함수
 *
 * 하나의 Page Table 내 모든 엔트리를 순회하며,
 * Present 비트가 설정된 엔트리에 대해 콜백 함수 func를 호출한다.
 *
 * 상위 단계의 인덱스(pml4_index, pdp_index, pdx_index)와 현재 PT 인덱스를
 * 조합하여 해당 엔트리에 대응하는 가상 주소(va)를 복원한다.
 *
 * 콜백이 false를 반환하면 순회를 즉시 중단하고 false를 반환한다.
 *
 * 반환값: 모든 엔트리를 성공적으로 순회하면 true, 중단되면 false.
 */
static bool
pt_for_each (uint64_t *pt, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index, unsigned pdx_index) {
	/* PT의 모든 엔트리(512개)를 순회 */
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pte = &pt[i];
		if (((uint64_t) *pte) & PTE_P) {
			/*
			 * 4단계 인덱스를 조합하여 이 엔트리에 대응하는 가상 주소를 복원.
			 * va = (PML4 인덱스 << 39) | (PDPE 인덱스 << 30)
			 *    | (PDX 인덱스 << 21) | (PTX 인덱스 << 12)
			 */
			void *va = (void *) (((uint64_t) pml4_index << PML4SHIFT) |
								 ((uint64_t) pdp_index << PDPESHIFT) |
								 ((uint64_t) pdx_index << PDXSHIFT) |
								 ((uint64_t) i << PTXSHIFT));
			/* 콜백 함수 호출. false 반환 시 순회 중단 */
			if (!func (pte, va, aux))
				return false;
		}
	}
	return true;
}

/*
 * pgdir_for_each - Page Directory(3단계) 순회 함수
 *
 * 하나의 Page Directory 내 모든 엔트리를 순회하며,
 * Present 비트가 설정된 엔트리에 대해 하위 Page Table을 pt_for_each로 순회한다.
 *
 * 반환값: 모든 하위 순회가 성공하면 true, 중단되면 false.
 */
static bool
pgdir_for_each (uint64_t *pdp, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index) {
	/* PD의 모든 엔트리(512개)를 순회 */
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		/* 물리 주소를 커널 가상 주소로 변환 */
		uint64_t *pte = ptov((uint64_t *) pdp[i]);
		if (((uint64_t) pte) & PTE_P)
			/* Present 비트가 설정된 경우, 해당 PT를 순회 */
			if (!pt_for_each ((uint64_t *) PTE_ADDR (pte), func, aux,
					pml4_index, pdp_index, i))
				return false;
	}
	return true;
}

/*
 * pdp_for_each - PDPT(2단계) 순회 함수
 *
 * 하나의 Page Directory Pointer Table 내 모든 엔트리를 순회하며,
 * Present 비트가 설정된 엔트리에 대해 하위 PD를 pgdir_for_each로 순회한다.
 *
 * 반환값: 모든 하위 순회가 성공하면 true, 중단되면 false.
 */
static bool
pdp_for_each (uint64_t *pdp,
		pte_for_each_func *func, void *aux, unsigned pml4_index) {
	/* PDPT의 모든 엔트리(512개)를 순회 */
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		/* 물리 주소를 커널 가상 주소로 변환 */
		uint64_t *pde = ptov((uint64_t *) pdp[i]);
		if (((uint64_t) pde) & PTE_P)
			/* Present 비트가 설정된 경우, 해당 PD를 순회 */
			if (!pgdir_for_each ((uint64_t *) PTE_ADDR (pde), func,
					 aux, pml4_index, i))
				return false;
	}
	return true;
}

/*
 * pml4_for_each - PML4(1단계, 최상위) 순회 함수
 *
 * 전체 4단계 페이지 테이블을 재귀적으로 순회한다.
 * PML4 -> PDPT -> PD -> PT 순서로 내려가며,
 * 커널 영역을 포함한 모든 유효한(Present) PTE에 대해 콜백 func를 호출한다.
 *
 * 콜백이 false를 반환하면 전체 순회를 즉시 중단한다.
 *
 * 반환값: 모든 엔트리를 성공적으로 순회하면 true, 중단되면 false.
 */
bool
pml4_for_each (uint64_t *pml4, pte_for_each_func *func, void *aux) {
	/* PML4의 모든 엔트리(512개)를 순회 */
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		/* 물리 주소를 커널 가상 주소로 변환 */
		uint64_t *pdpe = ptov((uint64_t *) pml4[i]);
		if (((uint64_t) pdpe) & PTE_P)
			/* Present 비트가 설정된 경우, 해당 PDPT를 순회 */
			if (!pdp_for_each ((uint64_t *) PTE_ADDR (pdpe), func, aux, i))
				return false;
	}
	return true;
}

/*
 * pt_destroy - Page Table(4단계, 최하위) 해제 함수
 *
 * Page Table의 모든 엔트리를 순회하며, Present 비트가 설정된 엔트리가
 * 가리키는 물리 페이지 프레임을 해제한다.
 * 마지막으로 Page Table 자체도 해제한다.
 *
 * 이 함수는 사용자 영역의 실제 데이터 페이지를 해제하는 최하위 단계이다.
 */
static void
pt_destroy (uint64_t *pt) {
	/* PT의 모든 엔트리를 순회하며 매핑된 물리 페이지를 해제 */
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pte = ptov((uint64_t *) pt[i]);
		if (((uint64_t) pte) & PTE_P)
			/* Present 비트가 설정된 엔트리가 가리키는 물리 페이지를 해제 */
			palloc_free_page ((void *) PTE_ADDR (pte));
	}
	/* Page Table 페이지 자체를 해제 */
	palloc_free_page ((void *) pt);
}

/*
 * pgdir_destroy - Page Directory(3단계) 해제 함수
 *
 * PD의 모든 엔트리를 순회하며, Present 비트가 설정된 엔트리가
 * 가리키는 하위 Page Table을 pt_destroy로 재귀적으로 해제한다.
 * 마지막으로 PD 자체도 해제한다.
 */
static void
pgdir_destroy (uint64_t *pdp) {
	/* PD의 모든 엔트리를 순회 */
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pte = ptov((uint64_t *) pdp[i]);
		if (((uint64_t) pte) & PTE_P)
			/* 하위 PT를 재귀적으로 해제 */
			pt_destroy (PTE_ADDR (pte));
	}
	/* Page Directory 페이지 자체를 해제 */
	palloc_free_page ((void *) pdp);
}

/*
 * pdpe_destroy - PDPT(2단계) 해제 함수
 *
 * PDPT의 모든 엔트리를 순회하며, Present 비트가 설정된 엔트리가
 * 가리키는 하위 Page Directory를 pgdir_destroy로 재귀적으로 해제한다.
 * 마지막으로 PDPT 자체도 해제한다.
 */
static void
pdpe_destroy (uint64_t *pdpe) {
	/* PDPT의 모든 엔트리를 순회 */
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pde = ptov((uint64_t *) pdpe[i]);
		if (((uint64_t) pde) & PTE_P)
			/* 하위 PD를 재귀적으로 해제 */
			pgdir_destroy ((void *) PTE_ADDR (pde));
	}
	/* PDPT 페이지 자체를 해제 */
	palloc_free_page ((void *) pdpe);
}

/*
 * pml4_destroy - PML4 페이지 테이블 전체를 해제한다.
 *
 * 프로세스 종료 시 호출되며, 해당 프로세스의 사용자 영역 페이지 테이블과
 * 매핑된 모든 물리 페이지를 재귀적으로 해제한다.
 *
 * PML4 인덱스 0번만 해제하는 이유:
 *   x86-64에서 PML4 인덱스가 1 이상인 영역은 커널 가상 주소 공간에 해당한다.
 *   커널 영역은 모든 프로세스가 공유하므로 개별 프로세스가 해제해서는 안 된다.
 *   사용자 영역은 PML4 인덱스 0에 해당하므로 이것만 해제한다.
 *
 * 안전 장치:
 *   - pml4가 NULL이면 아무것도 하지 않는다.
 *   - base_pml4(커널 기본 테이블)을 해제하려고 하면 ASSERT로 중단한다.
 */
void
pml4_destroy (uint64_t *pml4) {
	if (pml4 == NULL)
		return;
	/* 커널 기본 페이지 테이블(base_pml4)을 해제하는 것은 금지 */
	ASSERT (pml4 != base_pml4);

	/* PML4 인덱스 0번(사용자 영역)에 해당하는 PDPT만 해제한다.
	 * 인덱스 1 이상은 커널 영역이므로 해제하지 않는다. */
	uint64_t *pdpe = ptov ((uint64_t *) pml4[0]);
	if (((uint64_t) pdpe) & PTE_P)
		pdpe_destroy ((void *) PTE_ADDR (pdpe));
	/* 마지막으로 PML4 테이블 페이지 자체를 해제 */
	palloc_free_page ((void *) pml4);
}

/*
 * pml4_activate - PML4 페이지 테이블을 CPU에 활성화한다.
 *
 * x86-64에서 CR3 레지스터는 현재 활성화된 최상위 페이지 테이블(PML4)의
 * 물리 주소를 저장한다. 이 레지스터를 변경하면 CPU는 새로운 페이지 테이블을
 * 사용하여 가상 주소를 변환한다.
 *
 * CR3에 새 값을 쓰면 TLB(Translation Lookaside Buffer)가 자동으로
 * 전체 플러시(flush)된다. TLB는 최근 가상->물리 주소 변환 결과를
 * 캐싱하는 하드웨어이므로, 페이지 테이블이 바뀌면 캐시를 무효화해야 한다.
 *
 * pml4가 NULL이면 커널 기본 페이지 테이블(base_pml4)을 활성화한다.
 * lcr3()은 CR3 레지스터에 물리 주소를 쓰는 인라인 어셈블리 함수이다.
 */
void
pml4_activate (uint64_t *pml4) {
	/* pml4가 NULL이면 base_pml4를 사용. vtop으로 물리 주소 변환 후 CR3에 로드 */
	lcr3 (vtop (pml4 ? pml4 : base_pml4));
}

/*
 * pml4_get_page - 사용자 가상 주소에 매핑된 커널 가상 주소를 반환한다.
 *
 * 사용자 가상 주소 uaddr에 대해 4단계 페이지 테이블 워크를 수행하여
 * 매핑된 물리 페이지의 커널 가상 주소를 반환한다.
 * 페이지 내 오프셋(하위 12비트)도 보존된다.
 *
 * 매개변수:
 *   pml4  - 현재 프로세스의 PML4 테이블
 *   uaddr - 조회할 사용자 가상 주소
 *
 * 반환값:
 *   매핑이 존재하면 해당 물리 페이지의 커널 가상 주소 + 페이지 내 오프셋.
 *   매핑이 없으면 NULL.
 */
void *
pml4_get_page (uint64_t *pml4, const void *uaddr) {
	/* 사용자 영역 주소인지 확인 (커널 주소에 대해서는 호출 불가) */
	ASSERT (is_user_vaddr (uaddr));

	/* 4단계 워크를 수행하여 PT 엔트리를 찾음. create=0이므로 새 테이블을 만들지 않음 */
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) uaddr, 0);

	if (pte && (*pte & PTE_P))
		/*
		 * PT 엔트리에서 물리 주소를 추출(PTE_ADDR)하고,
		 * 커널 가상 주소로 변환(ptov)한 뒤,
		 * 원래 주소의 페이지 내 오프셋(pg_ofs, 하위 12비트)을 더한다.
		 */
		return ptov (PTE_ADDR (*pte)) + pg_ofs (uaddr);
	return NULL;
}

/*
 * pml4_set_page - 사용자 가상 페이지를 물리 프레임에 매핑한다.
 *
 * 사용자 가상 주소 upage를 커널 가상 주소 kpage가 가리키는 물리 프레임에
 * 매핑하는 PTE를 설정한다. 필요한 중간 테이블(PDPT, PD, PT)이 없으면
 * 자동으로 할당한다.
 *
 * 매개변수:
 *   pml4  - 현재 프로세스의 PML4 테이블
 *   upage - 매핑할 사용자 가상 페이지 주소 (페이지 정렬 필수)
 *   kpage - 매핑 대상 물리 프레임의 커널 가상 주소 (페이지 정렬 필수)
 *   rw    - true이면 읽기/쓰기, false이면 읽기 전용
 *
 * 사전 조건:
 *   - upage와 kpage 모두 페이지 경계에 정렬되어 있어야 한다.
 *   - upage는 사용자 영역 주소여야 한다.
 *   - upage는 아직 매핑되어 있지 않아야 한다.
 *   - base_pml4에 직접 매핑해서는 안 된다.
 *
 * 반환값: 성공 시 true, 메모리 할당 실패 시 false.
 */
bool
pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw) {
	/* 페이지 정렬 확인 (하위 12비트가 모두 0이어야 함) */
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (pg_ofs (kpage) == 0);
	/* 사용자 영역 주소인지 확인 */
	ASSERT (is_user_vaddr (upage));
	/* 커널 기본 페이지 테이블을 수정하는 것은 금지 */
	ASSERT (pml4 != base_pml4);

	/* 4단계 워크를 수행하여 PT 엔트리를 찾음. create=1이므로 없으면 새로 할당 */
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) upage, 1);

	if (pte)
		/*
		 * PT 엔트리에 물리 주소와 플래그를 설정:
		 *   vtop(kpage) - 커널 가상 주소를 물리 주소로 변환
		 *   PTE_P       - Present 비트 (페이지가 메모리에 존재)
		 *   PTE_W       - rw가 true이면 쓰기 허용
		 *   PTE_U       - 사용자 모드 접근 허용
		 */
		*pte = vtop (kpage) | PTE_P | (rw ? PTE_W : 0) | PTE_U;
	return pte != NULL;
}

/*
 * pml4_clear_page - 사용자 가상 페이지의 매핑을 무효화한다.
 *
 * 해당 PTE의 Present 비트를 해제하여 이후 접근 시 페이지 폴트가
 * 발생하도록 한다. PTE의 다른 비트(물리 주소, 플래그 등)는 보존된다.
 *
 * 현재 활성화된 페이지 테이블을 수정하는 경우, TLB에 캐싱된
 * 이전 변환 결과를 무효화(invlpg)해야 한다. 그렇지 않으면 CPU가
 * 여전히 이전 매핑을 사용할 수 있다.
 *
 * upage가 매핑되어 있지 않아도 에러를 발생시키지 않는다.
 */
void
pml4_clear_page (uint64_t *pml4, void *upage) {
	uint64_t *pte;
	/* 페이지 정렬 및 사용자 영역 주소 확인 */
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (is_user_vaddr (upage));

	/* 4단계 워크로 PT 엔트리를 찾음. create=false이므로 새 테이블을 만들지 않음 */
	pte = pml4e_walk (pml4, (uint64_t) upage, false);

	if (pte != NULL && (*pte & PTE_P) != 0) {
		/* Present 비트를 해제하여 매핑을 무효화 */
		*pte &= ~PTE_P;
		/*
		 * 현재 CR3에 로드된 페이지 테이블을 수정한 경우,
		 * invlpg 명령어로 해당 가상 주소의 TLB 엔트리를 무효화한다.
		 * rcr3()는 현재 CR3 값을 읽는 함수이다.
		 */
		if (rcr3 () == vtop (pml4))
			invlpg ((uint64_t) upage);
	}
}

/*
 * pml4_is_dirty - 가상 페이지의 Dirty 비트를 확인한다.
 *
 * Dirty 비트(PTE_D)는 해당 페이지에 쓰기가 발생하면 CPU 하드웨어가
 * 자동으로 1로 설정한다. 이 비트를 통해 PTE가 설치된 이후
 * 페이지가 수정되었는지 확인할 수 있다.
 *
 * 페이지 교체 시 dirty 페이지는 디스크에 기록해야 하고,
 * clean 페이지는 버려도 되므로 이 정보가 중요하다.
 *
 * 반환값: 해당 페이지가 수정되었으면 true, 아니면 false.
 *         PTE가 존재하지 않으면 false.
 */
bool
pml4_is_dirty (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	return pte != NULL && (*pte & PTE_D) != 0;
}

/*
 * pml4_set_dirty - 가상 페이지의 Dirty 비트를 설정하거나 해제한다.
 *
 * dirty가 true이면 PTE_D 비트를 설정하고,
 * false이면 해제한다.
 *
 * Dirty 비트를 변경한 후, 현재 활성화된 페이지 테이블의 경우
 * TLB 엔트리를 무효화(invlpg)하여 CPU가 변경된 PTE를 다시 읽도록 한다.
 * TLB에 이전 상태가 캐싱되어 있으면 변경이 반영되지 않을 수 있기 때문이다.
 */
void
pml4_set_dirty (uint64_t *pml4, const void *vpage, bool dirty) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	if (pte) {
		if (dirty)
			/* Dirty 비트를 설정 */
			*pte |= PTE_D;
		else
			/* Dirty 비트를 해제 */
			*pte &= ~(uint32_t) PTE_D;

		/* 현재 활성 페이지 테이블이면 해당 주소의 TLB 엔트리를 무효화 */
		if (rcr3 () == vtop (pml4))
			invlpg ((uint64_t) vpage);
	}
}

/*
 * pml4_is_accessed - 가상 페이지의 Accessed 비트를 확인한다.
 *
 * Accessed 비트(PTE_A)는 해당 페이지에 읽기 또는 쓰기 접근이 발생하면
 * CPU 하드웨어가 자동으로 1로 설정한다.
 *
 * 이 비트는 PTE가 설치된 이후 또는 마지막으로 비트가 초기화된 이후
 * 해당 페이지가 접근되었는지를 나타낸다.
 *
 * 페이지 교체 알고리즘(예: Clock 알고리즘)에서 최근에 사용된 페이지를
 * 식별하는 데 사용된다. 주기적으로 이 비트를 초기화하고 다시 설정되는지
 * 확인함으로써 페이지의 사용 빈도를 추정할 수 있다.
 *
 * 반환값: 해당 페이지가 접근되었으면 true, 아니면 false.
 *         PTE가 존재하지 않으면 false.
 */
bool
pml4_is_accessed (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	return pte != NULL && (*pte & PTE_A) != 0;
}

/*
 * pml4_set_accessed - 가상 페이지의 Accessed 비트를 설정하거나 해제한다.
 *
 * accessed가 true이면 PTE_A 비트를 설정하고,
 * false이면 해제한다.
 *
 * 페이지 교체 알고리즘에서 주기적으로 Accessed 비트를 false로 초기화하여
 * 이후 접근 여부를 추적하는 데 사용된다.
 *
 * 비트 변경 후 현재 활성 페이지 테이블이면 TLB를 무효화하여
 * CPU가 변경된 PTE를 반영하도록 한다.
 */
void
pml4_set_accessed (uint64_t *pml4, const void *vpage, bool accessed) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	if (pte) {
		if (accessed)
			/* Accessed 비트를 설정 */
			*pte |= PTE_A;
		else
			/* Accessed 비트를 해제 */
			*pte &= ~(uint32_t) PTE_A;

		/* 현재 활성 페이지 테이블이면 해당 주소의 TLB 엔트리를 무효화 */
		if (rcr3 () == vtop (pml4))
			invlpg ((uint64_t) vpage);
	}
}
