/* malloc() 의 간단한 구현 - Pintos 커널 동적 메모리 할당기
 *
 * ===== 전체 할당 전략 =====
 *
 * 이 할당기는 슬랩(slab) 방식과 유사한 구조를 사용한다.
 * 핵심 개념은 "디스크립터(descriptor)"와 "아레나(arena)"이다.
 *
 * 1) 디스크립터 (power-of-2 크기별 관리자)
 *    - 요청된 바이트 수를 2의 거듭제곱으로 올림하여,
 *      해당 크기를 관리하는 디스크립터에 할당을 위임한다.
 *    - 디스크립터 크기: 16, 32, 64, 128, 256, 512, 1024 바이트
 *      (PGSIZE가 4096일 때 PGSIZE/2 = 2048 미만까지, 총 7개)
 *    - descs[] 배열에 최대 10개까지 저장 가능하다.
 *    - 각 디스크립터는 해당 크기의 사용 가능한 블록들을 free list로 관리한다.
 *
 * 2) 아레나 (페이지 할당기로부터 받은 메모리 페이지)
 *    - free list가 비어 있으면, 페이지 할당기(palloc)에서 새 페이지를 받아
 *      "아레나"로 만든다.
 *    - 아레나 하나는 페이지 하나(4KB)에 대응하며,
 *      페이지 앞부분에 struct arena 헤더를 놓고,
 *      나머지 공간을 동일 크기 블록들로 분할한다.
 *    - 분할된 블록들은 모두 디스크립터의 free list에 추가된다.
 *
 * 3) 할당 흐름
 *    - malloc 요청 시 free list에서 블록 하나를 꺼내 반환한다.
 *    - free list가 비었으면 새 아레나를 생성한 뒤 블록을 꺼낸다.
 *
 * 4) 해제 흐름
 *    - free 시 블록을 다시 free list에 넣는다.
 *    - 아레나 내 모든 블록이 반환되면(free_cnt == blocks_per_arena),
 *      아레나의 모든 블록을 free list에서 제거하고
 *      아레나(페이지)를 페이지 할당기에 반환한다.
 *
 * 5) 2KB 초과 대형 블록 처리
 *    - 단일 페이지(4KB)에 아레나 헤더와 함께 넣기엔 너무 크므로,
 *      디스크립터를 거치지 않고 페이지 할당기에서 연속된 여러 페이지를
 *      직접 할당한다.
 *    - 이 경우 아레나 헤더의 desc 필드를 NULL로 설정하고,
 *      free_cnt에 할당된 페이지 수를 저장한다.
 *    - 해제 시에도 palloc_free_multiple()로 직접 반환한다.
 */

#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* 디스크립터(descriptor) 구조체.
 * 특정 블록 크기에 대한 할당/해제를 관리한다.
 * 2의 거듭제곱 크기마다 하나의 디스크립터가 존재한다.
 * (예: 16바이트용, 32바이트용, 64바이트용, ...) */
struct desc {
	size_t block_size;          /* 이 디스크립터가 관리하는 각 블록의 크기(바이트). */
	size_t blocks_per_arena;    /* 하나의 아레나(페이지)에 들어가는 블록 개수.
	                               계산식: (PGSIZE - sizeof(struct arena)) / block_size */
	struct list free_list;      /* 사용 가능한(해제된) 블록들의 연결 리스트. */
	struct lock lock;           /* 이 디스크립터에 대한 동기화 락.
	                               여러 스레드가 동시에 같은 크기 블록을
	                               할당/해제할 때 경쟁 조건을 방지한다. */
};

/* 아레나 손상 감지를 위한 매직 넘버.
 * 아레나의 magic 필드에 항상 이 값이 저장되어야 한다.
 * block_to_arena()에서 이 값을 검증하여,
 * 잘못된 포인터나 메모리 손상을 조기에 탐지한다.
 * 값 자체에 특별한 의미는 없으며, 임의로 선택된 상수이다. */
#define ARENA_MAGIC 0x9a548eed

/* 아레나(arena) 구조체.
 * 페이지 할당기에서 받은 하나의 페이지(또는 연속 페이지)에 대한 메타데이터.
 * 페이지의 맨 앞에 위치하며, 이후 공간이 블록들로 분할된다. */
struct arena {
	unsigned magic;             /* 항상 ARENA_MAGIC(0x9a548eed)이어야 한다.
	                               이 값이 아니면 메모리 손상이 발생한 것이다. */
	struct desc *desc;          /* 이 아레나를 소유하는 디스크립터에 대한 포인터.
	                               대형 블록(2KB 초과)의 경우 NULL로 설정된다.
	                               NULL이면 디스크립터를 거치지 않고
	                               직접 페이지 할당기로 관리되는 블록임을 의미한다. */
	size_t free_cnt;            /* 일반 블록: 이 아레나 내 사용 가능한 블록 수.
	                               대형 블록: 이 할당에 사용된 페이지 수. */
};

/* 해제된 블록(free block) 구조체.
 * 사용 중이 아닌 블록은 이 구조체로 취급되어
 * 디스크립터의 free list에 연결된다.
 * 블록이 할당되면 이 구조체는 사용자 데이터로 덮어쓰인다. */
struct block {
	struct list_elem free_elem; /* free list에 연결하기 위한 리스트 요소. */
};

/* 디스크립터 배열과 개수.
 * descs[]: 크기별 디스크립터를 저장하는 정적 배열 (최대 10개).
 *          malloc_init()에서 16, 32, 64, ..., PGSIZE/2 미만까지 초기화된다.
 *          PGSIZE가 4096이면 16, 32, 64, 128, 256, 512, 1024의 7개가 생성된다.
 * desc_cnt: 실제로 초기화된 디스크립터의 개수. */
static struct desc descs[10];   /* 디스크립터 배열. */
static size_t desc_cnt;         /* 초기화된 디스크립터 개수. */

static struct arena *block_to_arena (struct block *);
static struct block *arena_to_block (struct arena *, size_t idx);

/* malloc() 디스크립터들을 초기화한다.
 *
 * 블록 크기를 16바이트부터 시작하여 2배씩 증가시키면서
 * PGSIZE/2(보통 2048바이트) 미만까지 디스크립터를 생성한다.
 * PGSIZE/2 이상의 블록은 아레나 헤더와 함께 한 페이지에 들어갈 수 없으므로
 * 디스크립터를 만들지 않고 대형 블록 경로로 처리한다.
 *
 * 각 디스크립터에 대해:
 *   - block_size: 관리할 블록 크기 설정
 *   - blocks_per_arena: 한 페이지에서 아레나 헤더를 뺀 나머지를
 *                       block_size로 나누어 블록 개수 계산
 *   - free_list: 빈 리스트로 초기화
 *   - lock: 락 초기화 */
void
malloc_init (void) {
	size_t block_size;

	for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2) {
		struct desc *d = &descs[desc_cnt++];
		ASSERT (desc_cnt <= sizeof descs / sizeof *descs);
		d->block_size = block_size;
		d->blocks_per_arena = (PGSIZE - sizeof (struct arena)) / block_size;
		list_init (&d->free_list);
		lock_init (&d->lock);
	}
}

/* 최소 SIZE 바이트 이상의 새 블록을 할당하여 반환한다.
 * 메모리가 부족하면 NULL 포인터를 반환한다.
 *
 * 할당 과정:
 * 1) size가 0이면 NULL을 반환한다.
 * 2) 디스크립터 배열을 순회하여 size 이상의 블록을 관리하는
 *    가장 작은 디스크립터를 찾는다.
 * 3) 적합한 디스크립터가 없으면(size > 최대 디스크립터 크기):
 *    - 대형 블록으로 처리한다.
 *    - 필요한 페이지 수를 계산하여 palloc_get_multiple()로 할당한다.
 *    - 아레나 헤더를 설정하고(desc=NULL, free_cnt=페이지수),
 *      헤더 바로 뒤 주소를 반환한다.
 * 4) 적합한 디스크립터가 있으면:
 *    - 디스크립터의 락을 획득한다.
 *    - free list가 비어 있으면 새 아레나를 생성한다:
 *      a) palloc_get_page()로 페이지 하나를 할당한다.
 *      b) 아레나 헤더를 초기화한다.
 *      c) 아레나의 모든 블록을 free list에 추가한다.
 *    - free list 맨 앞에서 블록을 꺼낸다.
 *    - 해당 아레나의 free_cnt를 감소시킨다.
 *    - 락을 해제하고 블록을 반환한다. */
void *
malloc (size_t size) {
	struct desc *d;
	struct block *b;
	struct arena *a;

	/* 0바이트 요청은 NULL 포인터로 충족시킨다. */
	if (size == 0)
		return NULL;

	/* SIZE 바이트 요청을 만족하는 가장 작은 디스크립터를 찾는다. */
	for (d = descs; d < descs + desc_cnt; d++)
		if (d->block_size >= size)
			break;
	if (d == descs + desc_cnt) {
		/* SIZE가 어떤 디스크립터보다도 크다.
		   SIZE + 아레나 헤더를 담을 수 있도록 충분한 페이지를 할당한다. */
		size_t page_cnt = DIV_ROUND_UP (size + sizeof *a, PGSIZE);
		a = palloc_get_multiple (0, page_cnt);
		if (a == NULL)
			return NULL;

		/* 대형 블록임을 나타내도록 아레나를 초기화하고
		   (desc=NULL, free_cnt=페이지 수) 데이터 영역을 반환한다.
		   a + 1 은 아레나 헤더 바로 다음 주소이다. */
		a->magic = ARENA_MAGIC;
		a->desc = NULL;
		a->free_cnt = page_cnt;
		return a + 1;
	}

	lock_acquire (&d->lock);

	/* free list가 비어 있으면 새 아레나를 생성한다. */
	if (list_empty (&d->free_list)) {
		size_t i;

		/* 페이지 할당기에서 페이지 하나를 할당받는다. */
		a = palloc_get_page (0);
		if (a == NULL) {
			lock_release (&d->lock);
			return NULL;
		}

		/* 아레나를 초기화하고, 아레나 내의 모든 블록을 free list에 추가한다. */
		a->magic = ARENA_MAGIC;
		a->desc = d;
		a->free_cnt = d->blocks_per_arena;
		for (i = 0; i < d->blocks_per_arena; i++) {
			struct block *b = arena_to_block (a, i);
			list_push_back (&d->free_list, &b->free_elem);
		}
	}

	/* free list에서 블록 하나를 꺼내어 반환한다. */
	b = list_entry (list_pop_front (&d->free_list), struct block, free_elem);
	a = block_to_arena (b);
	a->free_cnt--;
	lock_release (&d->lock);
	return b;
}

/* A * B 바이트를 할당하고 0으로 초기화하여 반환한다.
 * 메모리가 부족하면 NULL 포인터를 반환한다.
 *
 * malloc()과의 차이점:
 * - 두 인자의 곱으로 크기를 지정한다 (배열 할당에 유용).
 * - 오버플로 검사를 수행한다 (a*b가 a나 b보다 작으면 오버플로).
 * - 할당된 메모리를 0으로 초기화한다 (memset). */
void *
calloc (size_t a, size_t b) {
	void *p;
	size_t size;

	/* 블록 크기를 계산하고 size_t 범위 내인지 확인한다.
	   곱셈 오버플로가 발생하면 NULL을 반환한다. */
	size = a * b;
	if (size < a || size < b)
		return NULL;

	/* 메모리를 할당하고 0으로 채운다. */
	p = malloc (size);
	if (p != NULL)
		memset (p, 0, size);

	return p;
}

/* BLOCK에 할당된 바이트 수를 반환한다.
 *
 * 일반 블록: 디스크립터의 block_size를 반환한다.
 *            (실제 요청보다 클 수 있다. 예: 50바이트 요청 시 64바이트 반환)
 * 대형 블록: (전체 페이지 크기) - (페이지 내 블록 오프셋)을 반환한다.
 *            pg_ofs(block)은 블록의 페이지 내 오프셋이다. */
static size_t
block_size (void *block) {
	struct block *b = block;
	struct arena *a = block_to_arena (b);
	struct desc *d = a->desc;

	return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs (block);
}

/* OLD_BLOCK의 크기를 NEW_SIZE 바이트로 변경하려 시도한다.
 * 이 과정에서 블록이 이동될 수 있다.
 * 성공하면 새 블록을 반환하고, 실패하면 NULL을 반환한다.
 *
 * 특수 경우:
 * - OLD_BLOCK이 NULL이면 malloc(NEW_SIZE)와 동일하게 동작한다.
 * - NEW_SIZE가 0이면 free(OLD_BLOCK)와 동일하게 동작한다.
 *
 * 구현 방식:
 * 1) 새 크기로 malloc()을 호출한다.
 * 2) 기존 데이터를 새 블록에 복사한다 (기존/새 크기 중 작은 쪽만큼).
 * 3) 기존 블록을 해제한다.
 * (현재 구현은 항상 새 블록을 할당하므로 축소 시에도 복사가 발생한다) */
void *
realloc (void *old_block, size_t new_size) {
	if (new_size == 0) {
		free (old_block);
		return NULL;
	} else {
		void *new_block = malloc (new_size);
		if (old_block != NULL && new_block != NULL) {
			size_t old_size = block_size (old_block);
			size_t min_size = new_size < old_size ? new_size : old_size;
			memcpy (new_block, old_block, min_size);
			free (old_block);
		}
		return new_block;
	}
}

/* malloc(), calloc(), realloc()로 할당된 블록 P를 해제한다.
 *
 * 해제 과정:
 * 1) P가 NULL이면 아무 작업도 하지 않는다.
 * 2) 블록이 속한 아레나와 디스크립터를 찾는다.
 * 3) 일반 블록(desc != NULL)인 경우:
 *    a) 디버그 모드에서 블록을 0xcc로 채운다 (use-after-free 버그 탐지용).
 *    b) 블록을 free list에 추가한다.
 *    c) 아레나의 모든 블록이 해제되었으면(free_cnt == blocks_per_arena):
 *       - 아레나의 모든 블록을 free list에서 제거한다.
 *       - 아레나(페이지)를 페이지 할당기에 반환한다.
 * 4) 대형 블록(desc == NULL)인 경우:
 *    - palloc_free_multiple()로 페이지들을 직접 반환한다. */
void
free (void *p) {
	if (p != NULL) {
		struct block *b = p;
		struct arena *a = block_to_arena (b);
		struct desc *d = a->desc;

		if (d != NULL) {
			/* 일반 블록이다. 여기서 처리한다. */

#ifndef NDEBUG
			/* 블록 전체를 0xcc 패턴으로 채운다.
			   0xcc는 디버깅용 "poison" 값으로, 해제된 메모리를
			   해제 후에 사용(use-after-free)하는 버그를 탐지하는 데 도움된다.
			   해제된 블록의 데이터가 0xcc로 덮어써지므로,
			   해제 후 접근 시 비정상적인 값이 관찰되어
			   버그를 쉽게 발견할 수 있다.
			   NDEBUG가 정의되면(릴리스 빌드) 이 코드는 비활성화된다. */
			memset (b, 0xcc, d->block_size);
#endif

			lock_acquire (&d->lock);

			/* 블록을 free list에 추가한다. */
			list_push_front (&d->free_list, &b->free_elem);

			/* 아레나가 완전히 미사용 상태가 되었으면 페이지를 반환한다.
			   free_cnt를 1 증가시킨 후, blocks_per_arena 이상이면
			   아레나의 모든 블록이 해제된 것이다. */
			if (++a->free_cnt >= d->blocks_per_arena) {
				size_t i;

				ASSERT (a->free_cnt == d->blocks_per_arena);
				/* 아레나의 모든 블록을 free list에서 제거한다. */
				for (i = 0; i < d->blocks_per_arena; i++) {
					struct block *b = arena_to_block (a, i);
					list_remove (&b->free_elem);
				}
				/* 아레나가 차지하던 페이지를 페이지 할당기에 반환한다. */
				palloc_free_page (a);
			}

			lock_release (&d->lock);
		} else {
			/* 대형 블록이다. 할당된 페이지들을 직접 해제한다.
			   free_cnt에 저장된 페이지 수만큼 반환한다. */
			palloc_free_multiple (a, a->free_cnt);
			return;
		}
	}
}

/* 블록 B가 속한 아레나를 찾아 반환한다.
 *
 * 아레나는 항상 페이지 경계에 위치하므로,
 * 블록 주소를 페이지 크기로 내림(pg_round_down)하면
 * 아레나 헤더의 시작 주소를 얻을 수 있다.
 *
 * 검증 사항:
 * 1) 아레나 주소가 NULL이 아닌지 확인한다.
 * 2) magic 필드가 ARENA_MAGIC인지 확인하여 아레나 손상을 탐지한다.
 * 3) 일반 블록(desc != NULL)의 경우:
 *    블록이 아레나 헤더 이후에 block_size 단위로 정렬되어 있는지 확인한다.
 *    (pg_ofs(b) - sizeof(arena)) % block_size == 0 이어야 한다.
 * 4) 대형 블록(desc == NULL)의 경우:
 *    블록이 아레나 헤더 바로 뒤에 위치하는지 확인한다.
 *    pg_ofs(b) == sizeof(arena) 이어야 한다. */
static struct arena *
block_to_arena (struct block *b) {
	struct arena *a = pg_round_down (b);

	/* 아레나가 유효한지 검증한다. */
	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);

	/* 블록이 아레나 내에서 올바르게 정렬되어 있는지 검증한다. */
	ASSERT (a->desc == NULL
			|| (pg_ofs (b) - sizeof *a) % a->desc->block_size == 0);
	ASSERT (a->desc != NULL || pg_ofs (b) == sizeof *a);

	return a;
}

/* 아레나 A 내에서 IDX번째 블록(0부터 시작)을 반환한다.
 *
 * 블록 주소 계산:
 *   아레나 시작 주소 + 아레나 헤더 크기 + (인덱스 * 블록 크기)
 *   = (uint8_t*)a + sizeof(struct arena) + idx * block_size
 *
 * 검증 사항:
 * 1) 아레나 포인터가 NULL이 아닌지 확인한다.
 * 2) magic 필드가 ARENA_MAGIC인지 확인한다.
 * 3) idx가 blocks_per_arena 미만인지 확인하여 범위 초과를 방지한다. */
static struct block *
arena_to_block (struct arena *a, size_t idx) {
	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);
	ASSERT (idx < a->desc->blocks_per_arena);
	return (struct block *) ((uint8_t *) a
			+ sizeof *a
			+ idx * a->desc->block_size);
}
