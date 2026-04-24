#ifndef THREADS_LOADER_H
#define THREADS_LOADER_H

/* PC BIOS가 고정한 상수. */
#define LOADER_BASE 0x7c00      /* 부트 섹터가 로드되는 물리 주소. */
#define LOADER_END  0x7e00      /* 부트 섹터 끝 물리 주소 (0x7C00 + 512). */

/* 커널 베이스 물리 주소. */
#define LOADER_KERN_BASE 0x8004000000

/* 모든 물리 메모리가 매핑되는 커널 가상 주소.
 * 커널은 이 주소 이상의 가상 공간에서 물리 메모리에 접근한다. */
#define LOADER_PHYS_BASE 0x200000

/* Multiboot 정보 구조체 위치.
 * E820 메모리 맵 데이터를 여기에 저장한다.
 * palloc_init()이 이 데이터를 읽어 페이지 풀 크기를 결정한다. */
#define MULTIBOOT_INFO       0x7000
#define MULTIBOOT_FLAG       MULTIBOOT_INFO              /* 플래그 (메모리 맵 유효 여부). */
#define MULTIBOOT_MMAP_LEN   MULTIBOOT_INFO + 44         /* 메모리 맵 총 바이트 수. */
#define MULTIBOOT_MMAP_ADDR  MULTIBOOT_INFO + 48         /* 메모리 맵 시작 주소. */

#define E820_MAP MULTIBOOT_INFO + 52                     /* E820 엔트리 시작 (크기 포함). */
#define E820_MAP4 MULTIBOOT_INFO + 56                    /* E820 엔트리 시작 (데이터). */

/* 부트 섹터 내 주요 물리 주소.
 * .org 지시문으로 512바이트 내에 정확한 위치에 배치된다. */
#define LOADER_SIG (LOADER_END - LOADER_SIG_LEN)         /* 0xAA55 BIOS 부트 시그니처 위치. */
#define LOADER_ARGS (LOADER_SIG - LOADER_ARGS_LEN)       /* 커맨드라인 인자 시작 위치. */
#define LOADER_ARG_CNT (LOADER_ARGS - LOADER_ARG_CNT_LEN) /* 인자 개수 위치. */

/* 부트 섹터 데이터 크기. */
#define LOADER_SIG_LEN 2        /* BIOS 시그니처 크기 (0xAA55). */
#define LOADER_ARGS_LEN 128     /* 커맨드라인 인자 버퍼 크기. */
#define LOADER_ARG_CNT_LEN 4    /* 인자 개수 필드 크기. */

/* loader.S가 정의하는 GDT 셀렉터.
 * 추가 셀렉터는 userprog/gdt.h에서 정의.
 *
 * 각 값은 GDT 인덱스 × 8 (엔트리 크기가 8바이트).
 * 하위 2비트 = RPL(요청 특권 레벨). */
#define SEL_NULL        0x00    /* 널 셀렉터 (CPU 요구사항, 사용 불가). */
#define SEL_KCSEG       0x08    /* 커널 코드 세그먼트 (Ring 0). */
#define SEL_KDSEG       0x10    /* 커널 데이터 세그먼트 (Ring 0). */
#define SEL_UDSEG       0x1B    /* 유저 데이터 세그먼트 (Ring 3, RPL=3). */
#define SEL_UCSEG       0x23    /* 유저 코드 세그먼트 (Ring 3, RPL=3). */
#define SEL_TSS         0x28    /* 태스크 상태 세그먼트. */
#define SEL_CNT         8       /* 세그먼트 총 개수. */

#endif /* threads/loader.h */
