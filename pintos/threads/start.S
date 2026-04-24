/*=======================================================================
 * start.S — 32비트 → 64비트 부트스트랩 코드
 *=======================================================================
 *
 * ▶ 역할
 *   loader.S가 디스크에서 커널을 메모리에 적재한 뒤 이 코드로 점프한다.
 *   이 시점에서 CPU는 아직 32비트 보호 모드(Protected Mode)이다.
 *   start.S는 다음 작업을 순서대로 수행한다:
 *
 *   1) CPUID 명령어로 64비트 롱 모드(Long Mode) 지원 여부 확인
 *   2) CR4 레지스터에서 PAE(Physical Address Extension) 활성화
 *   3) 부트용 4단계 페이지 테이블(PML4 → PDPT → PDE) 생성
 *   4) CR3에 PML4 물리 주소 설정
 *   5) MSR(Model Specific Register)로 Long Mode 활성화
 *   6) CR0에서 페이징(PG) 비트 켜기
 *   7) 64비트 GDT 로드 후 far jump로 64비트 코드 세그먼트 진입
 *   8) 스택 설정 후 main() 호출 → C 코드로 제어 이전
 *
 * ▶ 부트 시퀀스에서의 위치
 *   BIOS → loader.S(16비트→32비트) → start.S(32비트→64비트) → main()
 *
 * ▶ 메모리 매핑 전략
 *   부트 시점에는 아직 palloc/mmu.c가 초기화되지 않았으므로,
 *   정적으로 할당된 페이지 테이블(boot_pml4e 등)을 사용한다.
 *   2MB 대형 페이지(huge page)를 이용해 물리 0~256MB 범위를
 *   두 곳에 동시에 매핑한다:
 *     - 항등 매핑(identity): 가상 0x0 → 물리 0x0 (전환 직후용)
 *     - 커널 매핑: 가상 LOADER_KERN_BASE → 물리 0x0 (커널 실행용)
 *   main()에서 paging_init()이 호출되면 이 부트 페이지 테이블은
 *   새 것으로 교체된다.
 *
 * ▶ RELOC 매크로
 *   코드의 링크 주소(VMA)는 LOADER_KERN_BASE + 오프셋이지만,
 *   페이징 활성화 전에는 물리 주소(LMA)로 접근해야 한다.
 *   RELOC(x)는 심볼의 가상 주소에서 LOADER_KERN_BASE를 빼서
 *   물리 주소를 계산한다.
 *
 * ▶ Project 1 관련
 *   직접 수정할 일은 없다. 하지만 thread.c의 thread_launch()에서
 *   do_iret로 컨텍스트 스위칭할 때, 여기서 설정한 세그먼트 셀렉터
 *   (SEL_KCSEG, SEL_KDSEG)를 그대로 사용한다.
 *=======================================================================*/

#include "threads/loader.h"

/*-- 상수 정의 --*/
#define LONG_MODE (1 << 29)   /* CPUID EDX 비트 29: 64비트 롱 모드 지원 */
#define CR0_PE 0x00000001     /* CR0 비트 0: Protection Enable (보호 모드) */
#define CR0_PG (1 << 31)      /* CR0 비트 31: Paging Enable (페이징) */
#define CR4_PAE 0x20          /* CR4 비트 5: Physical Address Extension */
#define PTE_P 0x1             /* 페이지 테이블 엔트리: Present (존재) */
#define PTE_W 0x2             /* 페이지 테이블 엔트리: Writable (쓰기 가능) */
#define EFER_MSR 0xC0000080   /* EFER(Extended Feature Enable Register) MSR 번호 */
#define EFER_LME (1 << 8)    /* EFER 비트 8: Long Mode Enable */
#define EFER_SCE (1 << 0)    /* EFER 비트 0: System Call Enable */

/*-- RELOC(x): 가상 주소 x → 물리 주소로 변환 (페이징 전에 사용) --*/
#define RELOC(x) (x - LOADER_KERN_BASE)

/*=======================================================================
 * .entry 섹션 — 링커 스크립트(kernel.lds.S)에 의해 .text 최앞에 배치됨
 *=======================================================================*/
.section .entry

/*-- _start: 링커 진입점 (물리 주소 = bootstrap - LOADER_KERN_BASE) --*/
.globl _start
_start = RELOC(bootstrap)

.globl bootstrap
.func bootstrap
.code32

/*=======================================================================
 * bootstrap — 32비트 보호 모드 → 64비트 롱 모드 전환
 *=======================================================================
 * loader.S에서 여기로 점프한다. CPU는 32비트 보호 모드 상태.
 *
 * 단계:
 *   (1) CPUID 명령어 존재 여부 확인 (EFLAGS의 ID 비트 토글)
 *   (2) CPUID로 롱 모드 지원 여부 확인
 *   (3) PAE 활성화 → 페이지 테이블 설정 → CR3 로드
 *   (4) EFER MSR로 롱 모드 + 시스템콜 활성화
 *   (5) 페이징 켜기 → 64비트 GDT 로드 → far jump로 64비트 진입
 *=======================================================================*/
bootstrap:
	/*-- (1) EFLAGS의 ID 비트(비트 21) 토글로 CPUID 지원 확인 --*/
	pushf                     /* EFLAGS를 스택에 저장 */
	pop %eax                  /* EAX = 현재 EFLAGS */
	mov %ecx, %eax            /* ECX에 백업 */
	xor $0x200000, %eax       /* 비트 21(ID) 반전 */
	push %eax
	popf                      /* 반전된 값으로 EFLAGS 복원 */
	cmp %eax, %ebx
	jz no_long_mode           /* ID 비트 변경 불가 → CPUID 미지원 → 멈춤 */

	/*-- (2) CPUID로 롱 모드 지원 확인 --*/
	xor %eax, %eax            /* EAX=0: CPUID 기본 정보 조회 */
	cpuid                     /* EAX = 최대 CPUID 함수 번호 */
	cmp $1, %eax
	jb no_long_mode           /* 함수 1도 못 쓰면 → 롱 모드 불가 */
	test $LONG_MODE, %edx     /* EDX 비트 29: 롱 모드 지원? */

/*=======================================================================
 * PAE 활성화
 *   PAE(Physical Address Extension)는 64비트 페이징의 전제 조건이다.
 *   PAE를 켜면 페이지 테이블 엔트리가 4바이트 → 8바이트로 확장되어
 *   4GB 이상의 물리 메모리를 주소 지정할 수 있다.
 *=======================================================================*/
	movl %cr4, %eax
	orl $CR4_PAE, %eax        /* CR4.PAE = 1 */
	movl %eax, %cr4


/*=======================================================================
 * setup_page_table — 부트용 페이지 테이블 생성
 *=======================================================================
 * x86-64 4단계 페이지 테이블 구조:
 *   PML4 (Page Map Level 4)     — 512개 엔트리, 각 512GB 커버
 *    └→ PDPT (Page Dir Pointer) — 512개 엔트리, 각 1GB 커버
 *        └→ PDE (Page Dir Entry)— 512개 엔트리, 각 2MB 커버 (대형 페이지)
 *
 * 부트 시에는 2MB 대형 페이지를 사용하여 간단히 매핑한다:
 *   - boot_pml4e[0] → boot_pdpt1 → boot_pde1  (항등 매핑: 가상=물리)
 *   - boot_pml4e[1] → boot_pdpt2 → boot_pde2  (커널 매핑: 가상=KERN_BASE+물리)
 *   - 128개 × 2MB = 256MB 범위를 매핑
 *
 * 왜 항등 매핑이 필요한가?
 *   페이징을 켜는 순간, 다음 명령어의 주소가 갑자기 가상 주소로 해석된다.
 *   항등 매핑이 없으면 현재 실행 중인 코드의 물리 주소가 무효가 되어
 *   즉시 페이지 폴트로 크래시한다. 나중에 main() → paging_init()에서
 *   항등 매핑은 제거된다.
 *=======================================================================*/
setup_page_table:
	/*-- 단계 1: boot_pml4e 전체를 0으로 초기화 (4KB = 1024 dwords) --*/
	lea (RELOC(boot_pml4e)), %edi
	xor %eax, %eax
	mov $0x400, %ecx          /* 0x400 = 1024 (4096 바이트 / 4) */
	rep stosl (%edi)           /* EDI부터 1024개 dword를 0으로 채움 */

	/*-- 단계 2: PML4에 PDPT 엔트리 설정 --*/
	lea (RELOC(boot_pml4e)), %edi

	/* PML4[0] → boot_pdpt1 (항등 매핑용) */
	lea (RELOC(boot_pdpt1)), %ebx
	orl $(PTE_P | PTE_W), %ebx  /* Present + Writable 플래그 */
	mov %ebx, (%edi)             /* PML4[0] = &boot_pdpt1 | P | W */

	/* PML4[1] → boot_pdpt2 (커널 매핑용, 가상 주소 상위 비트가 다름) */
	lea (RELOC(boot_pdpt2)), %ebx
	orl $(PTE_P | PTE_W), %ebx
	mov %ebx, 8(%edi)           /* PML4[1] = &boot_pdpt2 | P | W */

	/*-- 단계 3: PDPT에 PDE 엔트리 설정 --*/
	/* boot_pdpt1[0] → boot_pde1 */
	lea (RELOC(boot_pdpt1)), %edi
	lea (RELOC(boot_pde1)), %ebx
	orl $(PTE_P | PTE_W), %ebx
	mov %ebx, (%edi)

	/* boot_pdpt2[0] → boot_pde2 */
	lea (RELOC(boot_pdpt2)), %edi
	lea (RELOC(boot_pde2)), %ebx
	orl $(PTE_P | PTE_W), %ebx
	mov %ebx, (%edi)

	/*-- 단계 4: PDE 128개를 2MB 대형 페이지로 채움 --*/
	/*
	 * 0x180 = PS(Page Size, 비트 7) | G(Global, 비트 8)
	 *   PS=1 → 이 PDE가 2MB 대형 페이지를 직접 가리킴 (하위 PT 없음)
	 *   G=1  → TLB에서 컨텍스트 스위칭 시에도 유지 (커널 매핑이므로)
	 *
	 * boot_pde1: 항등 매핑   (가상 0x000000 ~ 0x0FFFFFFF → 물리 동일)
	 * boot_pde2: 커널 매핑   (가상 KERN_BASE ~ KERN_BASE+256MB → 물리 0~256MB)
	 * boot_pde2는 +256 오프셋으로 시작: KERN_BASE 내 올바른 위치에 매핑
	 */
	mov $128, %ecx             /* 128개 엔트리 × 2MB = 256MB */
	lea (RELOC(boot_pde1)), %ebx
	lea (RELOC(boot_pde2)), %edx
	add $256, %edx             /* 커널 PDE는 오프셋 256 바이트 (엔트리 32)부터 */
	mov $(PTE_P | PTE_W | 0x180), %eax  /* 첫 번째 2MB 페이지: 물리 0x0 */

fill_pdes:
	mov %eax, (%ebx)           /* boot_pde1[i] = 물리 주소 | P | W | PS | G */
	mov %eax, (%edx)           /* boot_pde2[i+32] = 같은 물리 주소 */
	add $8, %ebx               /* 다음 PDE 엔트리 (8바이트씩) */
	add $8, %edx
	add $0x200000, %eax        /* 물리 주소 += 2MB */
	dec %ecx
	cmp $0, %ecx
	jne fill_pdes

	/*-- 단계 5: CR3에 PML4 물리 주소 로드 --*/
	lea (RELOC(boot_pml4e)), %eax
	mov %eax, %cr3             /* CR3 = PML4 베이스 → 이후 페이징 켜면 이 테이블 사용 */

/*=======================================================================
 * EFER MSR로 Long Mode + System Call 활성화
 *   EFER(Extended Feature Enable Register)는 MSR 0xC0000080에 있다.
 *   LME(Long Mode Enable) 비트를 켜면 페이징 활성화 시 롱 모드로 진입.
 *   SCE(System Call Enable)는 syscall/sysret 명령어 사용을 허용한다.
 *=======================================================================*/
	mov $EFER_MSR, %ecx        /* ECX = MSR 주소 */
	rdmsr                      /* EAX:EDX ← MSR 값 읽기 */
	orl $(EFER_LME | EFER_SCE), %eax  /* LME + SCE 비트 설정 */
	wrmsr                      /* MSR에 다시 쓰기 */

/*=======================================================================
 * 페이징 활성화
 *   CR0.PE(이미 켜져 있음) + CR0.PG를 함께 켜면:
 *   - EFER.LME=1인 상태이므로 → 롱 모드가 활성화된다.
 *   - 이 시점부터 CPU는 IA-32e 호환 모드(32비트 코드 실행 가능)로 동작.
 *   - 진정한 64비트 모드는 64비트 코드 세그먼트로 점프해야 완성된다.
 *=======================================================================*/
	mov %cr0, %eax
	or $(CR0_PE|CR0_PG), %eax
	mov %eax, %cr0

/*=======================================================================
 * 64비트 GDT 로드 및 롱 모드 진입
 *   lgdt로 64비트 GDT 디스크립터를 로드한 뒤,
 *   far return(lret)으로 64비트 코드 세그먼트(SEL_KCSEG)로 점프한다.
 *   이 점프가 실행되면 CPU는 진정한 64비트 롱 모드로 전환된다.
 *=======================================================================*/
	lea (RELOC(gdt_desc64)), %eax
	lgdt (%eax)                /* GDTR ← 64비트 GDT */
	mov $(entry_64 - LOADER_KERN_BASE), %eax  /* entry_64의 물리 주소 */
	push $SEL_KCSEG            /* CS 셀렉터 (64비트 코드 세그먼트) */
	push %eax                  /* 리턴 주소 = entry_64 */
	lret                       /* CS:RIP ← SEL_KCSEG:entry_64 → 64비트 진입! */
.endfunc

/*-- 롱 모드 미지원 시 무한 루프 (여기서 멈춤) --*/
no_long_mode:
	jmp no_long_mode

/*=======================================================================
 * 64비트 GDT (Global Descriptor Table)
 *   롱 모드에서는 세그먼테이션이 대부분 무시되지만,
 *   CS(Code Segment)의 L(Long) 비트가 1이어야 64비트 실행이 된다.
 *
 *   gdt64[0] = 0x0000000000000000  (널 세그먼트, 필수)
 *   gdt64[1] = 0x00af9a000000ffff  (64비트 코드 세그먼트)
 *     - L=1(Long), D=0, P=1, S=1, Type=Execute/Read
 *   gdt64[2] = 0x00af92000000ffff  (64비트 데이터 세그먼트)
 *     - P=1, S=1, Type=Read/Write
 *
 *   gdt_desc64: GDTR에 로드할 디스크립터 (크기 0x17 = 23 = 3엔트리×8-1)
 *=======================================================================*/
.p2align 2
gdt64:
  .quad 0                   /* [0] 널 세그먼트 (CPU가 요구) */
  .quad 0x00af9a000000ffff  /* [1] 코드 세그먼트 64비트 (SEL_KCSEG = 0x08) */
  .quad 0x00af92000000ffff  /* [2] 데이터 세그먼트 64비트 (SEL_KDSEG = 0x10) */
gdt_desc64:
  .word 0x17                /* GDT 크기 - 1 (바이트) */
  .quad RELOC(gdt64)        /* GDT 물리 주소 */

/*=======================================================================
 * 부트용 페이지 테이블 데이터 영역
 *   각 테이블은 4KB(0x1000) 크기이며 페이지 정렬(4KB boundary)되어야 한다.
 *   .p2align 12 → 2^12 = 4096 바이트 정렬
 *
 *   boot_pml4e : PML4 (레벨 4, 최상위)
 *   boot_pdpt1 : PDPT #1 — 항등 매핑용 (PML4[0] → 여기)
 *   boot_pdpt2 : PDPT #2 — 커널 매핑용 (PML4[1] → 여기)
 *   boot_pde1  : PDE #1  — 항등 매핑의 2MB 대형 페이지들
 *   boot_pde2  : PDE #2  — 커널 매핑의 2MB 대형 페이지들
 *=======================================================================*/
.p2align 12
.globl boot_pml4e
.globl boot_pdpt1
.globl boot_pdpt2
.globl boot_pde1
.globl boot_pde2

boot_pml4e:
  .space  0x1000            /* 4KB 영역, 0으로 초기화 */
boot_pdpt1:
  .space  0x1000
boot_pdpt2:
  .space  0x1000
boot_pde1:
  .space  0x1000
boot_pde2:
  .space  0x1000

/*=======================================================================
 * entry_64 — 64비트 롱 모드 진입점
 *=======================================================================
 * 드디어 64비트 코드 실행! 이제 할 일:
 *   1) RBP = 0 (프레임 포인터 초기화, 콜 스택 최상위 표시)
 *   2) RSP = LOADER_KERN_BASE + 0x1000 (부트 스택, 물리 0~0x1000 영역)
 *      → 이 스택은 임시용이며, thread_init()에서 진짜 커널 스택으로 교체된다.
 *   3) main() 호출 → init.c의 main()으로 제어 이전
 *      → main()은 리턴하지 않는다 (thread_exit() 또는 power_off()로 종료)
 *=======================================================================*/
.section .text
.code64
.globl entry_64
.func entry_64
entry_64:
	xor %rbp, %rbp                         /* RBP = 0 (프레임 체인 끝 표시) */
	movabs $(LOADER_KERN_BASE + 0x1000), %rsp  /* RSP = 부트 스택 꼭대기 */
	movabs $main, %rax                     /* RAX = main 함수 주소 */
	call *%rax                             /* main() 호출 → C 세계로! */
.endfunc
