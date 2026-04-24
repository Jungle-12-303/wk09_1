#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 물리 메모리 크기 (4KB 페이지 단위).
 * palloc_init()에서 E820 메모리 맵을 기반으로 설정된다. */
extern size_t ram_pages;

/* 커널 매핑만 포함된 4단계 페이지 맵 (PML4).
 * 유저 프로세스는 이것을 복사하여 자신의 페이지 테이블을 만든다. */
extern uint64_t *base_pml4;

/* -q 옵션: 커널 작업이 완료되면 자동 종료할지 여부.
 * pintos -- -q run alarm-multiple 처럼 사용. */
extern bool power_off_when_done;

void power_off (void) NO_RETURN;

#endif /* threads/init.h */
