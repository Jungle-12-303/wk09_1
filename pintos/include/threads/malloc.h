#ifndef THREADS_MALLOC_H
#define THREADS_MALLOC_H

#include <debug.h>
#include <stddef.h>

/* 동적 메모리 할당기.
 *
 * palloc이 4KB 페이지 단위 할당이라면,
 * malloc은 그 안에서 바이트 단위 할당을 제공한다.
 * malloc_init()은 palloc_init() 이후에 호출되어야 한다.
 *
 * 내부적으로 2의 거듭제곱 크기의 블록 풀을 관리한다.
 * (16, 32, 64, 128, ... 2048 바이트) */

void malloc_init (void);
void *malloc (size_t) __attribute__ ((malloc));   /* size 바이트 할당, NULL 실패. */
void *calloc (size_t, size_t) __attribute__ ((malloc)); /* 0으로 초기화된 배열 할당. */
void *realloc (void *, size_t);                   /* 기존 블록 크기 변경. */
void free (void *);                               /* 할당된 메모리 해제. */

#endif /* threads/malloc.h */
