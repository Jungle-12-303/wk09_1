/*=======================================================================
 * loader.S — 부트 섹터 (Boot Sector) 로더
 *=======================================================================
 *
 * ▶ 역할
 *   PC 전원이 켜지면 BIOS가 디스크의 첫 번째 섹터(512바이트)를
 *   물리 주소 0x7C00에 로드하고 실행한다. 이 코드가 바로 그 부트 섹터다.
 *
 *   이 로더가 수행하는 작업:
 *     1) 16비트 리얼 모드에서 시작
 *     2) A20 게이트 활성화 (1MB 이상 메모리 접근 허용)
 *     3) INT 15h/E820h로 메모리 맵 조회 (사용 가능한 메모리 영역 파악)
 *     4) GDT 설정 후 32비트 보호 모드로 전환
 *     5) IDE 컨트롤러를 직접 제어하여 디스크에서 커널을 메모리에 적재
 *     6) 커널 진입점(start.S의 bootstrap)으로 점프
 *
 * ▶ 부트 시퀀스에서의 위치
 *   BIOS → [loader.S] → start.S → main()
 *   16비트    16→32비트    32→64비트    C 코드
 *
 * ▶ 메모리 레이아웃 (부트 시점)
 *   0x00000 ~ 0x003FF : IVT (Interrupt Vector Table, 리얼 모드)
 *   0x00400 ~ 0x004FF : BDA (BIOS Data Area)
 *   0x07C00 ~ 0x07DFF : 이 부트 섹터가 로드되는 위치 (512바이트)
 *   0x07E00 ~ 0x0FFFF : 부트 스택 등 자유 영역
 *   0x10000 ~         : E820 메모리 맵 데이터
 *   0x100000 (1MB) ~  : 커널이 로드되는 위치 (LOADER_PHYS_BASE)
 *
 * ▶ Project 1 관련
 *   직접 수정할 일은 없다. 하지만 이 코드가 수집하는 E820 메모리 맵을
 *   palloc.c의 palloc_init()이 읽어서 커널/유저 페이지 풀의 크기를
 *   결정한다. 또한 커맨드라인 인자(arg_cnt, args)를 여기서 저장하며,
 *   init.c의 main()이 이를 읽어 부트 옵션을 파싱한다.
 *
 * ▶ 512바이트 제한
 *   부트 섹터는 정확히 512바이트여야 한다. 마지막 2바이트는
 *   BIOS 시그니처(0xAA55)로, 이것이 없으면 BIOS가 부팅을 거부한다.
 *   .org 지시문으로 데이터의 위치를 고정하고, 코드가 512바이트를
 *   초과하면 어셈블 에러가 난다.
 *
 * ▶ 출처
 *   MIT 6.828 과정의 코드에서 파생. 원본 저작권은 아래 참조.
 *=======================================================================*/

/* This file is derived from source code used in MIT's 6.828
   course.  The original copyright notice is reproduced in full
   below. */

/*
 * Copyright (C) 1997 Massachusetts Institute of Technology
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */

#include "threads/loader.h"

/*-- CR0 제어 레지스터 플래그 --*/
#define CR0_PE 0x00000001      /* Protection Enable: 보호 모드 활성화 */
#define CR0_EM 0x00000004      /* FPU Emulation: 부동소수점 명령어 트랩 */
#define CR0_PG 0x80000000      /* Paging: 페이징 활성화 */
#define CR0_WP 0x00010000      /* Write-Protect: 커널 모드에서도 쓰기 보호 적용 */


/*=======================================================================
 * start — 부트 섹터 진입점 (16비트 리얼 모드)
 *=======================================================================
 * BIOS가 0x7C00에 이 코드를 로드한 후 여기로 점프한다.
 * 이 시점에서 CPU는 16비트 리얼 모드이다.
 *=======================================================================*/
.globl start
start:

	.code16                    /* 16비트 코드 생성 */

/*-----------------------------------------------------------------------
 * 초기 설정
 *   cli  : 인터럽트 비활성화 (보호 모드 IDT 설정 전까지 인터럽트 처리 불가)
 *   cld  : 문자열 명령어 방향 = 증가 (rep stosl 등에서 사용)
 *-----------------------------------------------------------------------*/
	cli
	cld

/*-----------------------------------------------------------------------
 * 데이터 세그먼트 설정
 *   리얼 모드에서 세그먼트:오프셋 = 물리주소
 *   ES = DS = 0 → 0x0000:offset = offset (직접 물리 주소 접근)
 *-----------------------------------------------------------------------*/
	subw %ax, %ax              /* AX = 0 */
	movw %ax, %es
	movw %ax, %ds

/*-----------------------------------------------------------------------
 * 스택 설정
 *   SS = 0, SP = 0x7C00 (이 부트 코드 바로 아래)
 *   스택은 아래로 자란다 → 0x7C00에서 시작하여 0x0000 방향으로 성장
 *   이 스택은 BIOS 호출(INT 15h 등)에서 사용된다.
 *-----------------------------------------------------------------------*/
	movw %ax, %ss
	movw $0x7c00, %sp

/*=======================================================================
 * A20 게이트 활성화
 *=======================================================================
 * 역사적 이유로 IBM PC AT의 A20 주소선(비트 20)은 부팅 시 비활성화 상태다.
 * A20이 꺼져 있으면 주소의 비트 20이 항상 0으로 강제되어,
 * 홀수 MB 영역(1MB, 3MB, 5MB...)에 접근할 수 없다.
 * 8042 키보드 컨트롤러의 출력 포트를 통해 A20을 활성화한다.
 *
 * 포트 0x64: 키보드 컨트롤러 상태/명령 레지스터
 *   비트 1 = 입력 버퍼 full (1이면 바쁨, 0이면 명령 수신 가능)
 * 포트 0x60: 키보드 컨트롤러 데이터 레지스터
 *
 * 절차:
 *   1) 0x64 상태 폴링 (바쁘지 않을 때까지 대기)
 *   2) 0x64에 0xD1 전송 (출력 포트 쓰기 명령)
 *   3) 다시 상태 폴링
 *   4) 0x60에 0xDF 전송 (A20 활성화 + 기타 비트 유지)
 *=======================================================================*/

	/* 상태 폴링: 입력 버퍼가 비어질 때까지 대기 */
1:	inb $0x64, %al
	testb $0x2, %al            /* 비트 1 = 입력 버퍼 full? */
	jnz 1b                     /* 바쁘면 다시 폴링 */

	/* 출력 포트 쓰기 명령 전송 */
	movb $0xd1, %al
	outb %al, $0x64

	/* 다시 상태 폴링 */
1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

	/* A20 활성화: 0xDF = 1101_1111 (비트 1 = A20 enable) */
	movb $0xdf, %al
	outb %al, $0x60


/*=======================================================================
 * E820 메모리 맵 조회 (INT 15h / Function E820h)
 *=======================================================================
 * BIOS에게 물리 메모리의 사용 가능 영역을 질의한다.
 * x86-64에서는 단순한 "확장 메모리 크기" 조회로는 부족하므로
 * E820을 사용하여 전체 메모리 맵을 받는다.
 *
 * 입력:
 *   EAX = 0xE820          (함수 번호)
 *   EBX = 연속 값          (첫 호출: 0, 이후: BIOS가 반환한 값)
 *   ECX = 버퍼 크기        (24바이트)
 *   EDX = 0x534D4150      (매직: 'SMAP' 문자열)
 *   ES:DI = 결과 저장 버퍼 주소
 *
 * 출력 (각 24바이트 엔트리):
 *   +0:  base_addr (8바이트)  — 메모리 영역 시작 주소
 *   +8:  length    (8바이트)  — 영역 크기
 *   +16: type      (4바이트)  — 1=사용 가능, 2=예약, 3=ACPI 회수 가능 등
 *   +20: ACPI ext  (4바이트)  — 확장 속성
 *
 * EBX = 0이면 마지막 엔트리.
 * 이 데이터는 palloc.c의 palloc_init()에서 읽어
 * 커널/유저 페이지 풀의 범위를 결정한다.
 *=======================================================================*/
	mov $0xe820, %eax          /* 함수 번호 */
	mov $(E820_MAP4), %edi     /* 결과 저장 버퍼 (loader.h에 정의) */
	xor %ebx, %ebx             /* 연속 값 = 0 (첫 번째 호출) */
	mov $0x534d4150, %edx      /* 매직 넘버 'SMAP' */
	mov $24, %ecx              /* 엔트리 크기 = 24바이트 */
	int $0x15                  /* BIOS 호출 */
	cmp %eax, %edx             /* 성공 시 EAX = 'SMAP' */
	test %ebx, %ebx            /* EBX = 0이면 단일 엔트리 → 메모리 맵 실패 */
	je panic
	mov $24, %ebp              /* EBP = 지금까지 읽은 바이트 수 */

	/*-- E820 엔트리 반복 읽기 --*/
parse_e820:
	mov %ecx, -4(%edi)         /* 현재 엔트리 크기 저장 */
	add $24, %edi              /* 다음 엔트리 위치로 이동 */
	mov $0xe820, %eax
	mov $24, %ecx
	int $0x15                  /* 다음 엔트리 요청 */
	jc e820_parse_done         /* CF=1이면 마지막 엔트리 */
	add $24, %ebp              /* 총 바이트 수 누적 */
	test %ebx, %ebx
	jne parse_e820             /* EBX != 0이면 더 있음 */

	/*-- E820 파싱 완료: Multiboot 구조체에 결과 저장 --*/
e820_parse_done:
	mov %ecx, -4(%edi)
	movl $0x40, MULTIBOOT_FLAG              /* 플래그: 메모리 맵 유효 */
	movl %ebp, MULTIBOOT_MMAP_LEN           /* 메모리 맵 총 바이트 수 */
	movl $(E820_MAP), MULTIBOOT_MMAP_ADDR   /* 메모리 맵 시작 주소 */

/*=======================================================================
 * 보호 모드(Protected Mode)로 전환
 *=======================================================================
 * 16비트 리얼 모드에서는 1MB까지만 주소 지정 가능하다.
 * 커널은 1MB(LOADER_PHYS_BASE) 위치에 로드되므로 보호 모드가 필요하다.
 *
 * 전환 절차:
 *   1) GDT(Global Descriptor Table) 로드 — 보호 모드 필수
 *   2) CR0.PE = 1 설정 (보호 모드 활성화)
 *   3) Far jump로 32비트 코드 세그먼트에 진입
 *      → CS의 세그먼트 캐시를 갱신하기 위해 반드시 필요
 *=======================================================================*/

	/* GDTR에 GDT 디스크립터 로드 */
	/* data32 접두사: 16비트 모드에서 32비트 주소 로드 보장 */
	data32 lgdt gdtdesc

	/* CR0.PE = 1 → 보호 모드 활성화 */
	movl %cr0, %eax
	orl $CR0_PE, %eax
	movl %eax, %cr0

	/*-- Far jump → 32비트 코드 세그먼트(SEL_KCSEG)로 전환 --*/
	/*   이 점프가 CS를 16비트 → 32비트 세그먼트로 갱신한다.
	 *   data32 접두사: 16비트 모드에서 32비트 오프셋으로 점프 */
	data32 ljmp $SEL_KCSEG, $protcseg

/*=======================================================================
 * protcseg — 32비트 보호 모드 진입 완료
 *=======================================================================
 * 이제 32비트 코드가 실행된다.
 * 나머지 세그먼트 레지스터를 32비트 데이터 세그먼트로 재설정한다.
 *=======================================================================*/
	.code32                    /* 이제부터 32비트 코드 생성 */

protcseg:
	/* 모든 데이터 세그먼트를 커널 데이터 세그먼트(SEL_KDSEG)로 설정 */
	movw $SEL_KDSEG, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss

	/* 스택 포인터 재설정: 물리 1MB + 192KB 위치 */
	/* 커널 로드 영역(1MB~)과 겹치지 않는 안전한 위치 */
	movl $LOADER_PHYS_BASE + 0x30000, %esp

/*=======================================================================
 * IDE 디스크에서 커널 로드
 *=======================================================================
 * PIO(Programmed I/O) 모드로 IDE 컨트롤러를 직접 제어하여
 * 디스크에서 커널 이미지를 메모리에 적재한다.
 *
 * ▶ IDE PIO 포트 (Primary Channel)
 *   0x1F0 : 데이터 레지스터 (16비트 읽기/쓰기)
 *   0x1F2 : 섹터 수 레지스터
 *   0x1F3 : LBA 비트 0~7  (섹터 번호)
 *   0x1F4 : LBA 비트 8~15 (실린더 하위)
 *   0x1F5 : LBA 비트 16~23 (실린더 상위)
 *   0x1F6 : LBA 비트 24~27 + 드라이브 선택 + LBA 모드
 *   0x1F7 : 명령/상태 레지스터
 *   0x3F6 : 제어 레지스터 (인터럽트 비활성화 등)
 *
 * ▶ 상태 레지스터 (0x1F7) 비트
 *   비트 7 (BSY)  : 컨트롤러 바쁨
 *   비트 3 (DRQ)  : 데이터 전송 준비 완료
 *
 * ▶ LBA 주소 형식 (0x1F3~0x1F6의 하위 4비트)
 *   0xE0 = 1110_0000 → LBA 모드, 마스터 드라이브
 *
 * ▶ 로드 범위
 *   섹터 1부터 시작 (섹터 0은 이 부트 섹터 자체)
 *   KERNEL_LOAD_PAGES * 8 섹터를 읽음
 *   (1 페이지 = 4KB = 8 섹터(512B/섹터))
 *   목적지: 물리 주소 LOADER_PHYS_BASE (= 0x100000, 1MB)
 *=======================================================================*/

	movl $1, %ebx              /* 시작 섹터 = 1 (LBA) */
	movl $LOADER_PHYS_BASE, %edi  /* 목적지 = 1MB */

	/*-- IDE 인터럽트 비활성화 (폴링 모드 사용) --*/
	/*   제어 레지스터(0x3F6)의 비트 1 = nIEN (인터럽트 비활성화) */
	movw $0x3f6, %dx
	movb $0x02, %al
	outb %al, %dx

	/*-- 섹터 읽기 루프 --*/
read_sector:

	/*-- 컨트롤러 바쁨(BSY) 해제 대기 --*/
	movl $0x1f7, %edx
1:	inb %dx, %al
	testb $0x80, %al           /* BSY 비트 = 1? */
	jnz 1b                     /* 바쁘면 계속 폴링 */

	/*-- 섹터 수 레지스터에 1 기록 (한 번에 1섹터 읽기) --*/
	movl $0x1f2, %edx
	movb $1, %al
	outb %al, %dx

	/*-- LBA 주소 설정 --*/
	/*   EBX의 하위 28비트를 LBA 주소로 사용
	 *   상위 4비트 = 0xE0 (LBA 모드 + 마스터 드라이브) */
	movl %ebx, %eax
	andl $0x0fffffff, %eax     /* 28비트 LBA 추출 */
	orl $0xe0000000, %eax      /* LBA 모드 + Device 0 */

	/*-- LBA를 포트 0x1F3~0x1F6에 순서대로 출력 --*/
	/*   각 포트에 EAX의 한 바이트씩 (하위 → 상위) */
	movl $4, %ecx
1:	incw %dx                   /* 0x1F3, 0x1F4, 0x1F5, 0x1F6 순서 */
	outb %al, %dx
	shrl $8, %eax              /* 다음 바이트 */
	loop 1b

	/*-- READ SECTORS 명령(0x20) 전송 --*/
	incw %dx                   /* 0x1F7 = 명령 레지스터 */
	movb $0x20, %al
	outb %al, %dx

	/*-- 컨트롤러 바쁨 해제 대기 --*/
1:	inb %dx, %al
	testb $0x80, %al           /* BSY = 1? */
	jnz 1b

	/*-- 데이터 준비(DRQ) 대기 --*/
1:	inb %dx, %al
	testb $0x08, %al           /* DRQ = 1? */
	jz 1b                      /* 아직이면 계속 폴링 */

	/*-- 데이터 전송: 256 워드(= 512바이트 = 1섹터) 읽기 --*/
	/*   rep insw: 포트 0x1F0에서 16비트씩 EDI로 복사, ECX회 반복 */
	movl $256, %ecx
	movl $0x1f0, %edx
	rep insw

	/*-- 다음 섹터로 --*/
	incl %ebx
	cmpl $KERNEL_LOAD_PAGES*8 + 1, %ebx  /* 모든 커널 페이지 로드 완료? */
	jnz read_sector

/*=======================================================================
 * 커널 진입
 *   디스크에서 커널을 모두 읽었으므로, LOADER_PHYS_BASE(1MB)에 있는
 *   커널 코드(start.S의 bootstrap)로 점프한다.
 *   이후 start.S가 64비트 롱 모드로 전환하고 main()을 호출한다.
 *=======================================================================*/
	movl $LOADER_PHYS_BASE, %eax
	call *%eax                 /* start.S의 _start(= bootstrap) 호출 */
	jmp panic                  /* 리턴하면 안 됨 → panic */

/*=======================================================================
 * GDT (Global Descriptor Table) — 32비트 보호 모드용
 *=======================================================================
 * 보호 모드에서는 세그먼트 레지스터가 GDT 인덱스를 가리킨다.
 * 여기서는 최소한의 3개 엔트리만 정의:
 *
 * gdt[0] = 0x0000000000000000  (널 세그먼트, CPU 요구사항)
 * gdt[1] = 0x00cf9a000000ffff  (코드 세그먼트)
 *   → Base=0, Limit=4GB, Execute/Read, Ring 0, 32비트
 * gdt[2] = 0x00cf92000000ffff  (데이터 세그먼트)
 *   → Base=0, Limit=4GB, Read/Write, Ring 0, 32비트
 *
 * "플랫 모델" — Base=0, Limit=4GB이므로 사실상 세그먼테이션 무효화.
 * 모든 메모리가 하나의 세그먼트에 속하므로 오프셋 = 물리 주소.
 *
 * gdtdesc: LGDT 명령어에 전달되는 디스크립터
 *   크기 = 0x17 = 23 = (3 × 8) - 1 바이트
 *   주소 = gdt 라벨의 물리 주소
 *=======================================================================*/
gdt:
	.quad 0x0000000000000000	/* [0] 널 세그먼트 */
	.quad 0x00cf9a000000ffff	/* [1] 코드 세그먼트 (SEL_KCSEG = 0x08) */
	.quad 0x00cf92000000ffff	/* [2] 데이터 세그먼트 (SEL_KDSEG = 0x10) */

gdtdesc:
	.word	0x17			/* GDT 크기 - 1 (바이트) */
	.long	gdt			/* GDT 시작 물리 주소 */

/*=======================================================================
 * panic — 치명적 오류 시 메시지 출력 후 무한 루프
 *=======================================================================
 * 16비트 리얼 모드에서만 작동한다 (BIOS INT 10h 사용).
 * E820 메모리 맵 조회 실패 등의 상황에서 호출된다.
 *
 * INT 10h / AH=0x0E : TTY 모드 문자 출력
 *   AL = 출력할 문자, BH = 페이지 번호(0)
 *   lodsb: DS:SI에서 한 바이트씩 읽어 AL에 저장, SI 증가
 *=======================================================================*/
panic:  .code16                /* 리얼 모드 코드 */
	movw $panic_message, %si   /* SI = "Panic!" 문자열 주소 */
	movb $0xe, %ah             /* AH = 0x0E (TTY 출력 함수) */
	subb %bh, %bh              /* BH = 0 (페이지 0) */
1:	lodsb                      /* AL = *SI++ */
	test %al, %al              /* 널 종료? */
2:	jz 2b                      /* 0이면 무한 루프 (여기서 멈춤) */
	int $0x10                  /* BIOS에 문자 출력 요청 */
	jmp 1b                     /* 다음 문자 */

panic_message:
	.ascii "Panic!"
	.byte 0                    /* 널 종료 문자 */

/*=======================================================================
 * 커맨드라인 인자 영역
 *=======================================================================
 * 이 영역은 `pintos` 유틸리티(호스트 프로그램)가 디스크 이미지를
 * 생성할 때 기록한다. 커널의 init.c → read_command_line()이
 * 이 데이터를 읽어 부트 옵션을 파싱한다.
 *
 * .org : 현재 위치를 특정 오프셋으로 이동 (빈 공간은 0으로 채움)
 *   LOADER_ARG_CNT : 커맨드라인 인자 개수 (4바이트)
 *   LOADER_ARGS    : 인자 문자열 (128바이트)
 *
 * 예: pintos -- -q run alarm-multiple
 *   → arg_cnt = 4, args = "-q\0run\0alarm-multiple\0"
 *=======================================================================*/
	.org LOADER_ARG_CNT - LOADER_BASE
arg_cnt:
	.long 0                    /* 인자 개수 (pintos 유틸리티가 채움) */
	.org LOADER_ARGS - LOADER_BASE
args:
	.fill 0x80, 1, 0           /* 128바이트 인자 버퍼 (0으로 초기화) */

/*=======================================================================
 * 부트 섹터 시그니처
 *=======================================================================
 * 부트 섹터의 마지막 2바이트는 반드시 0xAA55여야 한다.
 * BIOS가 이 시그니처를 확인하고, 없으면 부팅을 거부한다.
 * .org로 정확히 510번째 바이트(= LOADER_SIG - LOADER_BASE)에 위치시킨다.
 *=======================================================================*/
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55               /* BIOS 부트 시그니처: 0x55AA (리틀 엔디언) */
