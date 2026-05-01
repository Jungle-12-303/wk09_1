#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* @lock
 * x86 하드웨어 페이지 테이블을 다루기 위한 함수와 매크로들.
 * 가상 주소에 대한 더 일반적인 함수와 매크로는 vaddr.h를 참고하라.
 *
 * 가상 주소는 아래와 같이 구성된다.
 *  63          48 47            39 38            30 29            21 20         12 11         0
 * +-------------+----------------+----------------+----------------+-------------+------------+
 * | Sign Extend |    Page-Map    | Page-Directory | Page-directory |  Page-Table |  Physical  |
 * |             | Level-4 Offset |    Pointer     |     Offset     |   Offset    |   Offset   |
 * +-------------+----------------+----------------+----------------+-------------+------------+
 *               |                |                |                |             |            |
 *               +------- 9 ------+------- 9 ------+------- 9 ------+----- 9 -----+---- 12 ----+
 *                                         Virtual Address
 */

#define PML4SHIFT 39UL
#define PDPESHIFT 30UL
#define PDXSHIFT  21UL
#define PTXSHIFT  12UL

#define PML4(la)  ((((uint64_t) (la)) >> PML4SHIFT) & 0x1FF)
#define PDPE(la) ((((uint64_t) (la)) >> PDPESHIFT) & 0x1FF)
#define PDX(la)  ((((uint64_t) (la)) >> PDXSHIFT) & 0x1FF)
#define PTX(la)  ((((uint64_t) (la)) >> PTXSHIFT) & 0x1FF)
#define PTE_ADDR(pte) ((uint64_t) (pte) & ~0xFFF)

/* @lock
 * 중요한 플래그들은 아래에 나열되어 있다.
 * PDE 또는 PTE가 "present" 상태가 아니면 다른 플래그들은 무시된다.
 * PDE 또는 PTE를 0으로 초기화하면 "not present"로 해석되며, 그것으로 충분하다.
 */
/* @lock
 * 플래그 비트들.
 */
#define PTE_FLAGS 0x00000000000000fffUL
/* @lock
 * 주소 비트들.
 */
#define PTE_ADDR_MASK  0xffffffffffffff000UL
/* @lock
 * OS가 사용할 수 있는 비트들.
 */
#define PTE_AVL   0x00000e00
/* @lock
 * 1이면 present, 0이면 not present.
 */
#define PTE_P 0x1
/* @lock
 * 1이면 read/write, 0이면 read-only.
 */
#define PTE_W 0x2
/* @lock
 * 1이면 user/kernel, 0이면 kernel only.
 */
#define PTE_U 0x4
/* @lock
 * 1이면 accessed, 0이면 not accessed.
 */
#define PTE_A 0x20
/* @lock
 * 1이면 dirty, 0이면 not dirty(PTE에만 해당).
 */
#define PTE_D 0x40

#endif /* threads/pte.h */
