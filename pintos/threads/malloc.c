

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


struct desc {
	size_t block_size;
	size_t blocks_per_arena;
	struct list free_list;
	struct lock lock;
};


#define ARENA_MAGIC 0x9a548eed


struct arena {
	unsigned magic;
	struct desc *desc;
	size_t free_cnt;
};


struct block {
	struct list_elem free_elem;
};


static struct desc descs[10];
static size_t desc_cnt;

static struct arena *block_to_arena (struct block *);
static struct block *arena_to_block (struct arena *, size_t idx);


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


void *
malloc (size_t size) {
	struct desc *d;
	struct block *b;
	struct arena *a;


	if (size == 0)
		return NULL;


	for (d = descs; d < descs + desc_cnt; d++)
		if (d->block_size >= size)
			break;
	if (d == descs + desc_cnt) {

		size_t page_cnt = DIV_ROUND_UP (size + sizeof *a, PGSIZE);
		a = palloc_get_multiple (0, page_cnt);
		if (a == NULL)
			return NULL;


		a->magic = ARENA_MAGIC;
		a->desc = NULL;
		a->free_cnt = page_cnt;
		return a + 1;
	}

	lock_acquire (&d->lock);


	if (list_empty (&d->free_list)) {
		size_t i;


		a = palloc_get_page (0);
		if (a == NULL) {
			lock_release (&d->lock);
			return NULL;
		}


		a->magic = ARENA_MAGIC;
		a->desc = d;
		a->free_cnt = d->blocks_per_arena;
		for (i = 0; i < d->blocks_per_arena; i++) {
			struct block *b = arena_to_block (a, i);
			list_push_back (&d->free_list, &b->free_elem);
		}
	}


	b = list_entry (list_pop_front (&d->free_list), struct block, free_elem);
	a = block_to_arena (b);
	a->free_cnt--;
	lock_release (&d->lock);
	return b;
}


void *
calloc (size_t a, size_t b) {
	void *p;
	size_t size;


	size = a * b;
	if (size < a || size < b)
		return NULL;


	p = malloc (size);
	if (p != NULL)
		memset (p, 0, size);

	return p;
}


static size_t
block_size (void *block) {
	struct block *b = block;
	struct arena *a = block_to_arena (b);
	struct desc *d = a->desc;

	return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs (block);
}


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


void
free (void *p) {
	if (p != NULL) {
		struct block *b = p;
		struct arena *a = block_to_arena (b);
		struct desc *d = a->desc;

		if (d != NULL) {


#ifndef NDEBUG

			memset (b, 0xcc, d->block_size);
#endif

			lock_acquire (&d->lock);


			list_push_front (&d->free_list, &b->free_elem);


			if (++a->free_cnt >= d->blocks_per_arena) {
				size_t i;

				ASSERT (a->free_cnt == d->blocks_per_arena);

				for (i = 0; i < d->blocks_per_arena; i++) {
					struct block *b = arena_to_block (a, i);
					list_remove (&b->free_elem);
				}

				palloc_free_page (a);
			}

			lock_release (&d->lock);
		} else {

			palloc_free_multiple (a, a->free_cnt);
			return;
		}
	}
}


static struct arena *
block_to_arena (struct block *b) {
	struct arena *a = pg_round_down (b);


	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);


	ASSERT (a->desc == NULL
			|| (pg_ofs (b) - sizeof *a) % a->desc->block_size == 0);
	ASSERT (a->desc != NULL || pg_ofs (b) == sizeof *a);

	return a;
}


static struct block *
arena_to_block (struct arena *a, size_t idx) {
	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);
	ASSERT (idx < a->desc->blocks_per_arena);
	return (struct block *) ((uint8_t *) a
			+ sizeof *a
			+ idx * a->desc->block_size);
}
