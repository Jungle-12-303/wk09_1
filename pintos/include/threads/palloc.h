#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stdint.h>
#include <stddef.h>

/* 페이지 할당 플래그.
 * 비트 OR로 조합 가능. 예: PAL_ZERO | PAL_USER */
enum palloc_flags {
	PAL_ASSERT = 001,           /* 할당 실패 시 커널 패닉. */
	PAL_ZERO = 002,             /* 페이지 내용을 0으로 초기화. */
	PAL_USER = 004              /* 유저 풀에서 할당 (기본은 커널 풀). */
};

/* 유저 풀에 넣을 최대 페이지 수.
 * 커맨드라인 옵션 "-ul"로 설정 가능. */
extern size_t user_page_limit;

uint64_t palloc_init (void);                     /* 페이지 할당기 초기화 (E820 기반). */
void *palloc_get_page (enum palloc_flags);       /* 1페이지(4KB) 할당. */
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
	/* 연속된 page_cnt 페이지 할당. */
void palloc_free_page (void *);                  /* 1페이지 해제. */
void palloc_free_multiple (void *, size_t page_cnt);
	/* 연속된 page_cnt 페이지 해제. */

#endif /* threads/palloc.h */
