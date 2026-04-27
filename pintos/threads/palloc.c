

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

/* @lock 페이지 할당기. 페이지 크기 또는 페이지의 배수 크기 단위로
   메모리를 내준다. 더 작은 크기 단위의 할당기는 malloc.h를 참고한다.

   시스템 메모리는 커널 풀과 사용자 풀이라는 두 "pool"로 나뉜다.
   사용자 풀은 사용자 가상 메모리 페이지용이고, 커널 풀은 그 외 모든
   용도에 쓰인다. 여기서의 생각은 사용자 프로세스가 심하게 스와핑하더라도
   커널은 자신의 작업을 위한 메모리를 가지고 있어야 한다는 것이다.

   기본적으로 시스템 RAM의 절반은 커널 풀에, 절반은 사용자 풀에 주어진다.
   커널 풀에는 매우 과한 양이지만, 시연 목적에는 괜찮다. */

/* @lock 메모리 풀. */
struct pool {
	struct lock lock;               /* @lock 상호 배제. */
	struct bitmap *used_map;        /* @lock 빈 페이지의 비트맵. */
	uint8_t *base;                  /* @lock 풀의 기준 주소. */
};


static struct pool kernel_pool, user_pool;


size_t user_page_limit = SIZE_MAX;


static void
init_pool (struct pool *p, void **bm_base, uint64_t start, uint64_t end);


static bool page_from_pool (const struct pool *, void *page);


struct multiboot_info {
	uint32_t flags;
	uint32_t mem_low;
	uint32_t mem_high;
	uint32_t __unused[8];
	uint32_t mmap_len;
	uint32_t mmap_base;
};


struct e820_entry {
	uint32_t size;
	uint32_t mem_lo;
	uint32_t mem_hi;
	uint32_t len_lo;
	uint32_t len_hi;
	uint32_t type;
};


struct area {
	uint64_t start;
	uint64_t end;
	uint64_t size;
};


#define BASE_MEM_THRESHOLD 0x100000


#define USABLE 1


#define ACPI_RECLAIMABLE 3


#define APPEND_HILO(hi, lo) (((uint64_t) ((hi)) << 32) + (lo))


static void
resolve_area_info (struct area *base_mem, struct area *ext_mem) {
	struct multiboot_info *mb_info = ptov (MULTIBOOT_INFO);
	struct e820_entry *entries = ptov (mb_info->mmap_base);
	uint32_t i;

	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {

			uint64_t start = APPEND_HILO (entry->mem_hi, entry->mem_lo);
			uint64_t size = APPEND_HILO (entry->len_hi, entry->len_lo);
			uint64_t end = start + size;
			printf("%llx ~ %llx %d\n", start, end, entry->type);


			struct area *area = start < BASE_MEM_THRESHOLD ? base_mem : ext_mem;


			if (area->size == 0) {
				*area = (struct area) {
					.start = start,
					.end = end,
					.size = size,
				};
			} else {

				if (area->start > start)
					area->start = start;

				if (area->end < end)
					area->end = end;

				area->size += size;
			}
		}
	}
}


static void
populate_pools (struct area *base_mem, struct area *ext_mem) {

	extern char _end;

	void *free_start = pg_round_up (&_end);


	uint64_t total_pages = (base_mem->size + ext_mem->size) / PGSIZE;

	uint64_t user_pages = total_pages / 2 > user_page_limit ?
		user_page_limit : total_pages / 2;

	uint64_t kern_pages = total_pages - user_pages;


	enum { KERN_START, KERN, USER_START, USER } state = KERN_START;
	uint64_t rem = kern_pages;
	uint64_t region_start = 0, end = 0, start, size, size_in_pg;

	struct multiboot_info *mb_info = ptov (MULTIBOOT_INFO);
	struct e820_entry *entries = ptov (mb_info->mmap_base);

	uint32_t i;
	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {

			start = (uint64_t) ptov (APPEND_HILO (entry->mem_hi, entry->mem_lo));
			size = APPEND_HILO (entry->len_hi, entry->len_lo);
			end = start + size;
			size_in_pg = size / PGSIZE;


			if (state == KERN_START) {
				region_start = start;
				state = KERN;
			}

			switch (state) {
				case KERN:

					if (rem > size_in_pg) {
						rem -= size_in_pg;
						break;
					}

					init_pool (&kernel_pool,
							&free_start, region_start, start + rem * PGSIZE);

					if (rem == size_in_pg) {
						rem = user_pages;
						state = USER_START;
					} else {

						region_start = start + rem * PGSIZE;
						rem = user_pages - size_in_pg + rem;
						state = USER;
					}
					break;
				case USER_START:

					region_start = start;
					state = USER;
					break;
				case USER:

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


	init_pool(&user_pool, &free_start, region_start, end);


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



			if (end < usable_bound)
				continue;


			start = (uint64_t)
				pg_round_up (start >= usable_bound ? start : usable_bound);
split:

			if (page_from_pool (&kernel_pool, (void *) start))
				pool = &kernel_pool;
			else if (page_from_pool (&user_pool, (void *) start))
				pool = &user_pool;
			else
				NOT_REACHED ();


			pool_end = pool->base + bitmap_size (pool->used_map) * PGSIZE;

			page_idx = pg_no (start) - pg_no (pool->base);
			if ((uint64_t) pool_end < end) {

				page_cnt = ((uint64_t) pool_end - start) / PGSIZE;
				bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
				start = (uint64_t) pool_end;
				goto split;
			} else {

				page_cnt = ((uint64_t) end - start) / PGSIZE;
				bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
			}
		}
	}
}


uint64_t
palloc_init (void) {

	extern char _end;

	struct area base_mem = { .size = 0 };
	struct area ext_mem = { .size = 0 };


	resolve_area_info (&base_mem, &ext_mem);
	printf ("Pintos booting with: \n");
	printf ("\tbase_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",
		  base_mem.start, base_mem.end, base_mem.size / 1024);
	printf ("\text_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",
		  ext_mem.start, ext_mem.end, ext_mem.size / 1024);

	populate_pools (&base_mem, &ext_mem);
	return ext_mem.end;
}


void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt) {

	struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;


	lock_acquire (&pool->lock);

	size_t page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
	lock_release (&pool->lock);
	void *pages;

	if (page_idx != BITMAP_ERROR)

		pages = pool->base + PGSIZE * page_idx;
	else
		pages = NULL;

	if (pages) {

		if (flags & PAL_ZERO)
			memset (pages, 0, PGSIZE * page_cnt);
	} else {

		if (flags & PAL_ASSERT)
			PANIC ("palloc_get: out of pages");
	}

	return pages;
}


void *
palloc_get_page (enum palloc_flags flags) {
	return palloc_get_multiple (flags, 1);
}


void
palloc_free_multiple (void *pages, size_t page_cnt) {
	struct pool *pool;
	size_t page_idx;


	ASSERT (pg_ofs (pages) == 0);
	if (pages == NULL || page_cnt == 0)
		return;


	if (page_from_pool (&kernel_pool, pages))
		pool = &kernel_pool;
	else if (page_from_pool (&user_pool, pages))
		pool = &user_pool;
	else
		NOT_REACHED ();


	page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG

	memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

	ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));

	bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}


void
palloc_free_page (void *page) {
	palloc_free_multiple (page, 1);
}


static void
init_pool (struct pool *p, void **bm_base, uint64_t start, uint64_t end) {

	uint64_t pgcnt = (end - start) / PGSIZE;

	size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (pgcnt), PGSIZE) * PGSIZE;


	lock_init(&p->lock);

	p->used_map = bitmap_create_in_buf (pgcnt, *bm_base, bm_pages);

	p->base = (void *) start;


	bitmap_set_all(p->used_map, true);


	*bm_base += bm_pages;
}


static bool
page_from_pool (const struct pool *pool, void *page) {
	size_t page_no = pg_no (page);
	size_t start_page = pg_no (pool->base);
	size_t end_page = start_page + bitmap_size (pool->used_map);
	return page_no >= start_page && page_no < end_page;
}
