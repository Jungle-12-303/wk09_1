#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* x86-64 하드웨어 페이지 테이블 관련 함수와 매크로.
 * 가상 주소에 대한 일반적인 함수/매크로는 vaddr.h 참고.
 *
 * x86-64 가상 주소 구조 (4단계 페이징):
 *  63          48 47            39 38            30 29            21 20         12 11         0
 * +-------------+----------------+----------------+----------------+-------------+------------+
 * | Sign Extend |    Page-Map    | Page-Directory | Page-directory |  Page-Table |  Physical  |
 * |             | Level-4 Offset |    Pointer     |     Offset     |   Offset    |   Offset   |
 * +-------------+----------------+----------------+----------------+-------------+------------+
 *               |                |                |                |             |            |
 *               +------- 9 ------+------- 9 ------+------- 9 ------+----- 9 -----+---- 12 ----+
 *                                         가상 주소
 *
 * 각 레벨의 인덱스는 9비트 (0~511), 물리 오프셋은 12비트 (0~4095).
 * 따라서 각 페이지 테이블은 512개 엔트리, 1페이지(4KB) = 512 × 8바이트.
 */

/* 각 페이지 테이블 레벨의 비트 시프트 값. */
#define PML4SHIFT 39UL          /* PML4 인덱스 시작 비트. */
#define PDPESHIFT 30UL          /* PDPE 인덱스 시작 비트. */
#define PDXSHIFT  21UL          /* PD 인덱스 시작 비트. */
#define PTXSHIFT  12UL          /* PT 인덱스 시작 비트. */

/* 가상 주소에서 각 레벨의 인덱스 추출 (9비트, 0~511). */
#define PML4(la)  ((((uint64_t) (la)) >> PML4SHIFT) & 0x1FF)
#define PDPE(la) ((((uint64_t) (la)) >> PDPESHIFT) & 0x1FF)
#define PDX(la)  ((((uint64_t) (la)) >> PDXSHIFT) & 0x1FF)
#define PTX(la)  ((((uint64_t) (la)) >> PTXSHIFT) & 0x1FF)

/* PTE에서 물리 주소 추출 (하위 12비트 플래그 제거). */
#define PTE_ADDR(pte) ((uint64_t) (pte) & ~0xFFF)

/* PTE 플래그 비트.
 * PDE나 PTE의 P(present) 비트가 0이면 나머지 플래그는 무시된다.
 * 0으로 초기화된 PDE/PTE는 "미존재"로 해석되므로 안전하다. */
#define PTE_FLAGS 0x00000000000000fffUL    /* 플래그 비트 마스크 (하위 12비트). */
#define PTE_ADDR_MASK  0xffffffffffffff000UL /* 주소 비트 마스크 (상위 52비트). */
#define PTE_AVL   0x00000e00             /* OS가 자유롭게 사용 가능한 비트. */
#define PTE_P 0x1                        /* Present: 1=존재, 0=미존재. */
#define PTE_W 0x2                        /* Writable: 1=읽기/쓰기, 0=읽기 전용. */
#define PTE_U 0x4                        /* User: 1=유저+커널 접근, 0=커널만. */
#define PTE_A 0x20                       /* Accessed: CPU가 접근 시 자동 설정. */
#define PTE_D 0x40                       /* Dirty: CPU가 쓰기 시 자동 설정 (PTE만). */

#endif /* threads/pte.h */
