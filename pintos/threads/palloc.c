/*
 * palloc.c - Pintos 페이지 할당기 (Page Allocator)
 *
 * 이 파일은 물리 메모리를 페이지 단위(4KB)로 관리하는 할당기를 구현한다.
 * malloc.h의 할당기가 페이지보다 작은 단위를 처리하는 반면,
 * 이 할당기는 페이지 크기 또는 페이지의 배수 단위로 메모리를 할당/해제한다.
 *
 * [메모리 풀 구조]
 * 시스템 물리 메모리는 두 개의 "풀(pool)"로 나뉜다:
 *   - 커널 풀 (kernel pool): 커널 자체의 데이터 구조, 스택 등에 사용
 *   - 사용자 풀 (user pool): 사용자 프로세스의 가상 메모리 페이지에 사용
 * 이렇게 분리하는 이유는 사용자 프로세스가 메모리를 과도하게 사용(스왑)하더라도
 * 커널이 자체 동작을 위한 메모리를 확보할 수 있도록 하기 위함이다.
 * 기본적으로 시스템 RAM의 절반을 커널 풀에, 나머지 절반을 사용자 풀에 할당한다.
 *
 * [비트맵 기반 추적]
 * 각 풀은 비트맵(bitmap)을 사용하여 페이지의 사용/미사용 상태를 추적한다.
 * 비트맵의 각 비트가 하나의 페이지에 대응하며,
 *   - true(1): 해당 페이지가 사용 중(할당됨)
 *   - false(0): 해당 페이지가 사용 가능(미할당)
 *
 * [E820 메모리 맵 파싱]
 * BIOS가 제공하는 E820 메모리 맵을 파싱하여 실제 사용 가능한 물리 메모리
 * 영역을 파악한다. E820 엔트리는 각 메모리 영역의 시작 주소, 크기, 유형을
 * 기술하며, 유형이 USABLE(1) 또는 ACPI_RECLAIMABLE(3)인 영역만
 * 할당 대상으로 사용한다. 이를 기반 메모리(base memory, < 1MB)와
 * 확장 메모리(extended memory, >= 1MB)로 분류한 뒤 풀을 구성한다.
 */

#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/*
 * struct pool - 메모리 풀 구조체
 *
 * 하나의 연속된 물리 메모리 영역을 관리하는 풀을 나타낸다.
 * 커널 풀과 사용자 풀 각각에 대해 하나씩 존재한다.
 *
 * @lock:     풀에 대한 동시 접근을 방지하는 상호 배제 락.
 *            페이지 할당/해제 시 반드시 이 락을 획득해야 한다.
 * @used_map: 각 페이지의 사용 여부를 추적하는 비트맵.
 *            true면 사용 중, false면 사용 가능.
 * @base:     이 풀이 관리하는 메모리 영역의 시작 주소(커널 가상 주소).
 */
struct pool {
	struct lock lock;               /* 상호 배제 락 */
	struct bitmap *used_map;        /* 페이지 사용 여부 비트맵 */
	uint8_t *base;                  /* 풀의 시작 주소 */
};

/* 두 개의 메모리 풀: 커널 데이터용과 사용자 페이지용 */
static struct pool kernel_pool, user_pool;

/* 사용자 풀에 할당할 최대 페이지 수. 기본값은 SIZE_MAX(제한 없음). */
size_t user_page_limit = SIZE_MAX;

/* init_pool 함수의 전방 선언: 풀을 초기화한다. */
static void
init_pool (struct pool *p, void **bm_base, uint64_t start, uint64_t end);

/* page_from_pool 함수의 전방 선언: 페이지가 해당 풀에 속하는지 확인한다. */
static bool page_from_pool (const struct pool *, void *page);

/*
 * struct multiboot_info - Multiboot 정보 구조체
 *
 * 부트로더(GRUB 등)가 커널에 전달하는 시스템 정보를 담는 구조체.
 * E820 메모리 맵에 접근하기 위해 사용된다.
 *
 * @flags:    유효한 필드를 나타내는 플래그 비트마스크
 * @mem_low:  하위 메모리 크기 (KB 단위, 최대 640KB)
 * @mem_high: 상위 메모리 크기 (KB 단위, 1MB 이상)
 * @__unused: 사용하지 않는 예약 필드 8개 (boot_device, cmdline 등)
 * @mmap_len: E820 메모리 맵 테이블의 전체 바이트 크기
 * @mmap_base: E820 메모리 맵 테이블의 물리 주소
 */
struct multiboot_info {
	uint32_t flags;
	uint32_t mem_low;
	uint32_t mem_high;
	uint32_t __unused[8];
	uint32_t mmap_len;
	uint32_t mmap_base;
};

/*
 * struct e820_entry - E820 메모리 맵 엔트리
 *
 * BIOS INT 15h, AX=E820h 호출로 얻는 메모리 영역 정보 하나를 나타낸다.
 * 각 엔트리는 물리 메모리의 한 영역에 대한 시작 주소, 크기, 유형을 기술한다.
 *
 * @size:   이 엔트리 자체의 크기 (바이트, size 필드 자신은 제외)
 * @mem_lo: 메모리 영역 시작 주소의 하위 32비트
 * @mem_hi: 메모리 영역 시작 주소의 상위 32비트
 * @len_lo: 메모리 영역 크기의 하위 32비트
 * @len_hi: 메모리 영역 크기의 상위 32비트
 * @type:   메모리 영역의 유형
 *          1 = USABLE (OS가 자유롭게 사용 가능)
 *          2 = RESERVED (사용 불가)
 *          3 = ACPI_RECLAIMABLE (ACPI 테이블, OS가 회수 가능)
 *          4 = ACPI_NVS (비휘발성 ACPI 저장소)
 *          5 = BAD (불량 메모리)
 */
struct e820_entry {
	uint32_t size;
	uint32_t mem_lo;
	uint32_t mem_hi;
	uint32_t len_lo;
	uint32_t len_hi;
	uint32_t type;
};

/*
 * struct area - 메모리 영역 범위 정보
 *
 * 기반 메모리(base memory) 또는 확장 메모리(extended memory)의
 * 전체 범위를 요약하여 저장하는 구조체.
 * 여러 E820 엔트리를 하나로 병합한 결과를 담는다.
 *
 * @start: 영역의 시작 물리 주소 (가장 낮은 주소)
 * @end:   영역의 끝 물리 주소 (가장 높은 주소)
 * @size:  실제 사용 가능한 메모리의 총합 (바이트 단위)
 *         (start~end 범위 내에서 구멍(hole)이 있을 수 있으므로
 *          end - start와 다를 수 있다)
 */
struct area {
	uint64_t start;
	uint64_t end;
	uint64_t size;
};

/* 기반 메모리와 확장 메모리를 구분하는 경계값: 1MB (0x100000) */
#define BASE_MEM_THRESHOLD 0x100000

/* E820 메모리 유형 상수: 사용 가능한 메모리 */
#define USABLE 1

/* E820 메모리 유형 상수: ACPI 회수 가능 메모리 */
#define ACPI_RECLAIMABLE 3

/* 상위 32비트와 하위 32비트를 합쳐 64비트 주소를 만드는 매크로 */
#define APPEND_HILO(hi, lo) (((uint64_t) ((hi)) << 32) + (lo))

/*
 * resolve_area_info - E820 메모리 맵을 파싱하여 기반/확장 메모리 영역 정보를 수집
 *
 * BIOS가 제공하는 E820 메모리 맵의 모든 엔트리를 순회하면서,
 * 사용 가능한(USABLE) 또는 ACPI 회수 가능한(ACPI_RECLAIMABLE) 영역을
 * 기반 메모리(< 1MB)와 확장 메모리(>= 1MB)로 분류한다.
 * 같은 분류에 속하는 여러 엔트리는 하나의 area 구조체로 병합된다.
 *
 * @base_mem: [출력] 기반 메모리 영역 정보가 저장될 구조체 포인터.
 *            호출 전 size를 0으로 초기화해야 한다.
 * @ext_mem:  [출력] 확장 메모리 영역 정보가 저장될 구조체 포인터.
 *            호출 전 size를 0으로 초기화해야 한다.
 *
 * 동작 방식:
 *   1. Multiboot 정보 구조체에서 E820 맵의 위치와 크기를 얻는다.
 *   2. 각 E820 엔트리에 대해:
 *      - 유형이 USABLE 또는 ACPI_RECLAIMABLE이 아니면 무시
 *      - 시작 주소가 1MB 미만이면 base_mem에, 이상이면 ext_mem에 분류
 *      - 해당 area의 첫 엔트리면 그대로 저장, 이후 엔트리면 범위를 확장
 */
static void
resolve_area_info (struct area *base_mem, struct area *ext_mem) {
	struct multiboot_info *mb_info = ptov (MULTIBOOT_INFO);
	struct e820_entry *entries = ptov (mb_info->mmap_base);
	uint32_t i;

	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {
			/* 상위/하위 32비트를 합쳐 64비트 시작 주소와 크기를 계산 */
			uint64_t start = APPEND_HILO (entry->mem_hi, entry->mem_lo);
			uint64_t size = APPEND_HILO (entry->len_hi, entry->len_lo);
			uint64_t end = start + size;
			printf("%llx ~ %llx %d\n", start, end, entry->type);

			/* 시작 주소가 1MB 미만이면 기반 메모리, 이상이면 확장 메모리 */
			struct area *area = start < BASE_MEM_THRESHOLD ? base_mem : ext_mem;

			// 이 영역(base_mem 또는 ext_mem)에 속하는 첫 번째 엔트리인 경우
			if (area->size == 0) {
				*area = (struct area) {
					.start = start,
					.end = end,
					.size = size,
				};
			} else {  // 이미 엔트리가 존재하는 경우: 범위를 확장
				// 시작 주소를 더 낮은 값으로 확장
				if (area->start > start)
					area->start = start;
				// 끝 주소를 더 높은 값으로 확장
				if (area->end < end)
					area->end = end;
				// 사용 가능한 총 크기를 누적
				area->size += size;
			}
		}
	}
}

/*
 * populate_pools - 커널 풀과 사용자 풀을 실제 메모리에 배치하고 초기화
 *
 * E820 메모리 맵을 기반으로 커널 풀과 사용자 풀의 범위를 결정하고,
 * 각 풀을 초기화한 뒤, 사용 가능한 페이지들을 비트맵에 표시한다.
 *
 * @base_mem: resolve_area_info()에서 수집한 기반 메모리 영역 정보
 * @ext_mem:  resolve_area_info()에서 수집한 확장 메모리 영역 정보
 *
 * 동작 방식:
 *   1. 전체 페이지 수를 계산하고 절반을 사용자 풀, 나머지를 커널 풀에 배정.
 *      (user_page_limit으로 사용자 풀 크기를 제한할 수 있음)
 *   2. 상태 머신(KERN_START -> KERN -> USER_START -> USER)을 사용하여
 *      E820 엔트리들을 순회하면서 커널 풀과 사용자 풀의 물리 메모리 범위를 결정.
 *      - 커널 풀이 먼저 할당되고, 나머지가 사용자 풀에 할당됨
 *   3. 각 풀에 대해 init_pool()을 호출하여 비트맵과 락을 초기화.
 *   4. 다시 E820 맵을 순회하면서, 비트맵/메타데이터가 차지하는 공간 이후의
 *      실제 사용 가능한 페이지들을 비트맵에서 false(사용 가능)로 표시.
 *
 * 모든 페이지는 이 할당기가 관리하며, 코드 페이지까지 포함한다.
 * 기반 메모리 부분은 가능한 한 커널 풀에 포함시킨다.
 */
static void
populate_pools (struct area *base_mem, struct area *ext_mem) {
	/* _end: 링커가 기록한 커널 이미지의 끝 주소 (kernel.lds.S 참고) */
	extern char _end;
	/* 커널 이미지 끝 이후, 페이지 경계로 올림한 주소부터 자유 메모리 시작 */
	void *free_start = pg_round_up (&_end);

	/* 전체 사용 가능한 페이지 수 계산 */
	uint64_t total_pages = (base_mem->size + ext_mem->size) / PGSIZE;
	/* 사용자 풀 페이지 수: 전체의 절반 또는 user_page_limit 중 작은 값 */
	uint64_t user_pages = total_pages / 2 > user_page_limit ?
		user_page_limit : total_pages / 2;
	/* 커널 풀 페이지 수: 전체에서 사용자 풀을 뺀 나머지 */
	uint64_t kern_pages = total_pages - user_pages;

	/*
	 * 상태 머신으로 E820 맵을 순회하며 각 풀의 메모리 범위를 결정한다.
	 * 상태 전이: KERN_START -> KERN -> USER_START -> USER
	 *   - KERN_START: 커널 풀 시작 지점을 찾는 중
	 *   - KERN: 커널 풀에 페이지를 할당하는 중
	 *   - USER_START: 사용자 풀 시작 지점을 찾는 중
	 *   - USER: 사용자 풀에 페이지를 할당하는 중
	 */
	enum { KERN_START, KERN, USER_START, USER } state = KERN_START;
	uint64_t rem = kern_pages;  /* 현재 풀에 남은 할당 페이지 수 */
	uint64_t region_start = 0, end = 0, start, size, size_in_pg;

	struct multiboot_info *mb_info = ptov (MULTIBOOT_INFO);
	struct e820_entry *entries = ptov (mb_info->mmap_base);

	uint32_t i;
	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {
			/* 물리 주소를 커널 가상 주소로 변환하고 크기를 계산 */
			start = (uint64_t) ptov (APPEND_HILO (entry->mem_hi, entry->mem_lo));
			size = APPEND_HILO (entry->len_hi, entry->len_lo);
			end = start + size;
			size_in_pg = size / PGSIZE;

			/* 커널 풀 시작 상태: 현재 엔트리의 시작을 커널 풀 시작으로 기록 */
			if (state == KERN_START) {
				region_start = start;
				state = KERN;
			}

			switch (state) {
				case KERN:
					/* 이 엔트리의 페이지 수가 남은 커널 페이지보다 적으면
					   남은 수에서 차감하고 다음 엔트리로 진행 */
					if (rem > size_in_pg) {
						rem -= size_in_pg;
						break;
					}
					/* 커널 풀 생성: region_start부터 현재 위치 + 남은 페이지까지 */
					init_pool (&kernel_pool,
							&free_start, region_start, start + rem * PGSIZE);
					/* 상태 전이: 커널 페이지가 이 엔트리에서 정확히 끝나면
					   다음 엔트리부터 사용자 풀 시작 */
					if (rem == size_in_pg) {
						rem = user_pages;
						state = USER_START;
					} else {
						/* 이 엔트리의 나머지 부분부터 사용자 풀 시작 */
						region_start = start + rem * PGSIZE;
						rem = user_pages - size_in_pg + rem;
						state = USER;
					}
					break;
				case USER_START:
					/* 사용자 풀 시작 지점을 현재 엔트리의 시작으로 설정 */
					region_start = start;
					state = USER;
					break;
				case USER:
					/* 이 엔트리의 페이지 수가 남은 사용자 페이지보다 적으면
					   남은 수에서 차감하고 다음 엔트리로 진행 */
					if (rem > size_in_pg) {
						rem -= size_in_pg;
						break;
					}
					ASSERT (rem == size);
					break;
				default:
					NOT_REACHED ();
			}
		}
	}

	// 사용자 풀 생성: region_start부터 마지막 엔트리의 끝까지
	init_pool(&user_pool, &free_start, region_start, end);

	/*
	 * E820 엔트리를 다시 순회하면서 실제 사용 가능한 페이지들을
	 * 비트맵에서 false(사용 가능)로 표시한다.
	 *
	 * usable_bound: 비트맵 등 메타데이터가 차지하는 공간의 끝.
	 * 이 주소 이전의 페이지는 이미 사용 중이므로 할당 가능으로 표시하지 않는다.
	 */
	uint64_t usable_bound = (uint64_t) free_start;
	struct pool *pool;
	void *pool_end;
	size_t page_idx, page_cnt;

	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {
			uint64_t start = (uint64_t)
				ptov (APPEND_HILO (entry->mem_hi, entry->mem_lo));
			uint64_t size = APPEND_HILO (entry->len_hi, entry->len_lo);
			uint64_t end = start + size;

			// TODO: 0x1000 ~ 0x200000 영역 추가 필요. 현재는 문제되지 않음.
			// 이 엔트리 전체가 사용 가능 경계(usable_bound) 이전이면 건너뜀
			if (end < usable_bound)
				continue;

			/* 시작 주소를 usable_bound 이상, 페이지 경계로 올림 */
			start = (uint64_t)
				pg_round_up (start >= usable_bound ? start : usable_bound);
split:
			/*
			 * 현재 시작 주소가 어느 풀에 속하는지 판별한다.
			 * 하나의 E820 엔트리가 커널 풀과 사용자 풀에 걸쳐 있을 수 있으므로,
			 * 그 경우 split 레이블로 돌아와 나머지를 다른 풀에서 처리한다.
			 */
			if (page_from_pool (&kernel_pool, (void *) start))
				pool = &kernel_pool;
			else if (page_from_pool (&user_pool, (void *) start))
				pool = &user_pool;
			else
				NOT_REACHED ();

			/* 현재 풀의 끝 주소 계산 */
			pool_end = pool->base + bitmap_size (pool->used_map) * PGSIZE;
			/* 풀 내에서의 페이지 인덱스 계산 */
			page_idx = pg_no (start) - pg_no (pool->base);
			if ((uint64_t) pool_end < end) {
				/* 이 엔트리가 현재 풀의 끝을 넘어가는 경우:
				   풀 끝까지만 사용 가능으로 표시하고, 나머지는 split으로 분할 */
				page_cnt = ((uint64_t) pool_end - start) / PGSIZE;
				bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
				start = (uint64_t) pool_end;
				goto split;
			} else {
				/* 이 엔트리가 현재 풀 안에 완전히 포함되는 경우:
				   해당 범위를 모두 사용 가능으로 표시 */
				page_cnt = ((uint64_t) end - start) / PGSIZE;
				bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
			}
		}
	}
}

/*
 * palloc_init - 페이지 할당기를 초기화하고 메모리 크기를 반환
 *
 * 시스템 부팅 초기에 호출되어 페이지 할당기를 설정한다.
 * E820 메모리 맵을 파싱하여 기반/확장 메모리 영역을 파악하고,
 * 커널 풀과 사용자 풀을 구성한다.
 *
 * 반환값: 확장 메모리의 끝 주소 (물리 메모리의 상한)
 */
uint64_t
palloc_init (void) {
  /* 링커가 기록한 커널 이미지의 끝 주소. kernel.lds.S 참고. */
	extern char _end;
	/* 기반 메모리와 확장 메모리 영역 정보를 0으로 초기화 */
	struct area base_mem = { .size = 0 };
	struct area ext_mem = { .size = 0 };

	/* E820 맵을 파싱하여 메모리 영역 정보 수집 */
	resolve_area_info (&base_mem, &ext_mem);
	printf ("Pintos booting with: \n");
	printf ("\tbase_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",
		  base_mem.start, base_mem.end, base_mem.size / 1024);
	printf ("\text_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",
		  ext_mem.start, ext_mem.end, ext_mem.size / 1024);
	/* 수집한 정보를 바탕으로 커널 풀과 사용자 풀 구성 */
	populate_pools (&base_mem, &ext_mem);
	return ext_mem.end;
}

/*
 * palloc_get_multiple - 연속된 여러 페이지를 할당
 *
 * PAGE_CNT개의 연속된 빈 페이지를 찾아 할당하고, 첫 페이지의
 * 커널 가상 주소를 반환한다.
 *
 * @flags:    할당 옵션 플래그의 비트 조합:
 *            - PAL_USER:   설정 시 사용자 풀에서 할당, 미설정 시 커널 풀에서 할당.
 *            - PAL_ZERO:   설정 시 할당된 페이지를 0으로 초기화.
 *            - PAL_ASSERT: 설정 시 할당 실패하면 커널 패닉 발생.
 *                          미설정 시 할당 실패하면 NULL 반환.
 * @page_cnt: 할당할 연속 페이지 수
 *
 * 반환값: 할당된 첫 페이지의 커널 가상 주소.
 *         할당 실패 시 NULL (PAL_ASSERT가 설정되어 있으면 패닉).
 *
 * 동작 방식:
 *   1. PAL_USER 플래그에 따라 사용자 풀 또는 커널 풀을 선택
 *   2. 풀의 락을 획득한 뒤 비트맵에서 연속된 빈 페이지를 탐색
 *   3. 찾으면 비트맵에서 해당 비트들을 true(사용 중)로 설정
 *   4. PAL_ZERO 플래그가 설정되어 있으면 페이지를 0으로 초기화
 *   5. 찾지 못하면 PAL_ASSERT에 따라 NULL 반환 또는 패닉
 */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt) {
	/* PAL_USER 플래그에 따라 사용자 풀 또는 커널 풀 선택 */
	struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;

	/* 비트맵 접근 시 동기화를 위해 락 획득 */
	lock_acquire (&pool->lock);
	/* 비트맵에서 연속된 page_cnt개의 빈(false) 페이지를 찾고,
	   찾으면 해당 비트들을 true(사용 중)로 뒤집음 */
	size_t page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
	lock_release (&pool->lock);
	void *pages;

	if (page_idx != BITMAP_ERROR)
		/* 풀의 시작 주소에서 페이지 인덱스만큼 오프셋을 더해 실제 주소 계산 */
		pages = pool->base + PGSIZE * page_idx;
	else
		pages = NULL;

	if (pages) {
		/* PAL_ZERO 플래그가 설정되어 있으면 할당된 영역을 0으로 채움 */
		if (flags & PAL_ZERO)
			memset (pages, 0, PGSIZE * page_cnt);
	} else {
		/* 할당 실패: PAL_ASSERT 플래그가 설정되어 있으면 커널 패닉 */
		if (flags & PAL_ASSERT)
			PANIC ("palloc_get: out of pages");
	}

	return pages;
}

/*
 * palloc_get_page - 단일 페이지를 할당
 *
 * 하나의 빈 페이지를 할당하고 그 커널 가상 주소를 반환한다.
 * palloc_get_multiple(flags, 1)의 편의 래퍼 함수이다.
 *
 * @flags: 할당 옵션 플래그 (PAL_USER, PAL_ZERO, PAL_ASSERT)
 *         - PAL_USER:   사용자 풀에서 할당 (미설정 시 커널 풀)
 *         - PAL_ZERO:   할당된 페이지를 0으로 초기화
 *         - PAL_ASSERT: 할당 실패 시 커널 패닉 (미설정 시 NULL 반환)
 *
 * 반환값: 할당된 페이지의 커널 가상 주소, 실패 시 NULL.
 */
void *
palloc_get_page (enum palloc_flags flags) {
	return palloc_get_multiple (flags, 1);
}

/*
 * palloc_free_multiple - 여러 페이지를 해제
 *
 * PAGES에서 시작하는 PAGE_CNT개의 연속된 페이지를 해제한다.
 * 해제된 페이지는 이후 palloc_get_*으로 다시 할당될 수 있다.
 *
 * @pages:    해제할 페이지 블록의 시작 주소 (페이지 정렬되어 있어야 함)
 * @page_cnt: 해제할 연속 페이지 수
 *
 * 동작 방식:
 *   1. pages가 페이지 경계에 정렬되어 있는지 확인 (ASSERT)
 *   2. pages가 NULL이거나 page_cnt가 0이면 아무것도 하지 않음
 *   3. pages가 커널 풀 또는 사용자 풀 중 어디에 속하는지 판별
 *   4. 디버그 빌드에서는 해제할 영역을 0xCC로 채워서
 *      해제 후 접근 시 쉽게 감지할 수 있도록 함
 *   5. 해당 페이지들이 실제로 할당 상태(used)인지 확인 (ASSERT)
 *   6. 비트맵에서 해당 비트들을 false(사용 가능)로 설정
 */
void
palloc_free_multiple (void *pages, size_t page_cnt) {
	struct pool *pool;
	size_t page_idx;

	/* pages가 페이지 경계에 정렬되어 있는지 확인 */
	ASSERT (pg_ofs (pages) == 0);
	if (pages == NULL || page_cnt == 0)
		return;

	/* 해제할 페이지가 어느 풀에 속하는지 판별 */
	if (page_from_pool (&kernel_pool, pages))
		pool = &kernel_pool;
	else if (page_from_pool (&user_pool, pages))
		pool = &user_pool;
	else
		NOT_REACHED ();

	/* 풀 내에서의 페이지 인덱스 계산 */
	page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
	/* 디버그 빌드: 해제된 메모리를 0xCC 패턴으로 채워서
	   use-after-free 버그를 쉽게 감지할 수 있도록 함 */
	memset (pages, 0xcc, PGSIZE * page_cnt);
#endif
	/* 해제하려는 페이지들이 실제로 사용 중 상태인지 확인 */
	ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
	/* 비트맵에서 해당 페이지들을 사용 가능(false)으로 표시 */
	bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}

/*
 * palloc_free_page - 단일 페이지를 해제
 *
 * PAGE가 가리키는 하나의 페이지를 해제한다.
 * palloc_free_multiple(page, 1)의 편의 래퍼 함수이다.
 *
 * @page: 해제할 페이지의 커널 가상 주소 (페이지 정렬 필수)
 */
void
palloc_free_page (void *page) {
	palloc_free_multiple (page, 1);
}

/*
 * init_pool - 메모리 풀을 초기화
 *
 * 주어진 메모리 범위에 대해 풀 구조체를 설정한다.
 * 비트맵을 bm_base가 가리키는 위치에 생성하고,
 * 모든 페이지를 사용 불가(true)로 초기 설정한다.
 * (이후 populate_pools()에서 실제 사용 가능한 페이지를 false로 변경)
 *
 * @p:       초기화할 풀 구조체 포인터
 * @bm_base: [입출력] 비트맵을 배치할 메모리 주소의 포인터.
 *           함수 반환 시 비트맵 크기만큼 증가된다.
 * @start:   이 풀이 관리할 메모리 영역의 시작 주소 (커널 가상 주소)
 * @end:     이 풀이 관리할 메모리 영역의 끝 주소 (커널 가상 주소)
 *
 * 동작 방식:
 *   1. 풀이 관리할 총 페이지 수를 계산
 *   2. 비트맵에 필요한 공간을 계산하고, bm_base 위치에 비트맵 생성
 *   3. 풀의 락을 초기화
 *   4. 모든 비트를 true(사용 불가)로 설정
 *   5. bm_base를 비트맵 크기만큼 전진시켜 다음 데이터 구조가
 *      비트맵 뒤에 배치되도록 함
 */
static void
init_pool (struct pool *p, void **bm_base, uint64_t start, uint64_t end) {
	/* 이 풀이 관리할 총 페이지 수 계산 */
	uint64_t pgcnt = (end - start) / PGSIZE;
	/* 비트맵에 필요한 공간을 페이지 단위로 올림하여 계산 */
	size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (pgcnt), PGSIZE) * PGSIZE;

	/* 풀의 상호 배제 락 초기화 */
	lock_init(&p->lock);
	/* bm_base 위치에 비트맵을 생성 (별도 메모리 할당 없이 기존 버퍼 사용) */
	p->used_map = bitmap_create_in_buf (pgcnt, *bm_base, bm_pages);
	/* 풀의 시작 주소 설정 */
	p->base = (void *) start;

	// 모든 페이지를 사용 불가(true)로 초기화
	bitmap_set_all(p->used_map, true);

	/* bm_base를 비트맵 크기만큼 전진 */
	*bm_base += bm_pages;
}

/*
 * page_from_pool - 페이지가 특정 풀에 속하는지 판별
 *
 * 주어진 페이지 주소가 지정된 풀의 관리 범위 안에 있는지 확인한다.
 *
 * @pool: 확인할 풀 구조체 포인터
 * @page: 확인할 페이지의 커널 가상 주소
 *
 * 반환값: page가 pool의 범위 안에 있으면 true, 아니면 false
 *
 * 동작 방식:
 *   풀의 시작 페이지 번호와 끝 페이지 번호를 계산하고,
 *   주어진 페이지의 번호가 그 범위 안에 있는지 비교한다.
 */
static bool
page_from_pool (const struct pool *pool, void *page) {
	size_t page_no = pg_no (page);
	size_t start_page = pg_no (pool->base);
	size_t end_page = start_page + bitmap_size (pool->used_map);
	return page_no >= start_page && page_no < end_page;
}
