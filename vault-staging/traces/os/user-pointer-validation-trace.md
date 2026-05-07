---
type: Trace
status: Draft
week:
  - user-programs
systems:
  - Linux
  - Windows
  - PintOS
  - QEMU
tags:
  - domain:os
  - domain:pintos
  - domain:qemu
  - week:user-programs
  - layer:user
  - layer:kernel
  - layer:cpu
  - layer:memory
  - topic:syscall
  - topic:page-table
  - topic:page-fault
related_to:
  - "[[week-2-user-programs-map]]"
  - "[[syscall-end-to-end]]"
  - "[[address-translation-memory]]"
---
# 유저 포인터 검증 (User Pointer Validation) Trace

## 작은 질문

유저 프로그램이 `write(fd, buffer, size)`를 호출했을 때, 커널은 `buffer`가 가리키는 메모리를 “그냥 믿고” 읽어도 될까?

답은 **절대 안 된다**다.

유저 프로그램이 넘기는 포인터는:

- `NULL`일 수 있고
- 커널 주소를 가리킬 수도 있고
- 아직 매핑되지 않은(=페이지 테이블에 없는) 주소일 수도 있고
- `buffer`는 정상인데 `buffer + size - 1`이 다른 페이지로 넘어가며 터질 수도 있다

이 문서는 “유저 포인터 검증”이 정확히 어떤 문제를 막는지, 그리고 PintOS/QEMU 코드에서 그 흔적을 어디서 확인하는지 Trace로 정리한다.

## 왜 필요한가

운영체제는 유저 프로그램을 “신뢰하지 않는다”.

신뢰하면 생기는 문제:

- 보안: 유저가 커널 메모리를 읽거나 쓰게 된다(권한 붕괴).
- 안정성: 유저가 잘못된 포인터를 넘기면 커널이 page fault로 죽는다(커널 패닉/전체 다운).
- 일관성: 유저가 `size`를 크게 줘서 여러 페이지를 가로지르면 중간에만 터질 수 있다.

즉, “시스템 콜은 커널의 출입문”이고, **포인터 검증은 출입문 앞의 신분 확인**이다.

## 핵심 모델 (머릿속에 넣을 최소 모델)

유저 포인터 검증은 보통 다음 2단계를 분리해서 생각하면 된다.

1) **범위 검사**: “이 주소가 유저 주소 범위인가?” (`is_user_vaddr`)
2) **매핑 검사**: “현재 프로세스 페이지 테이블에서 이 주소가 실제 물리 프레임으로 매핑되는가?” (`pml4_get_page`)

여기서 주의:

- 범위가 유저라고 해서 매핑이 존재하는 건 아니다.
- `buffer`만 검사하면 부족하다. `buffer + size - 1`과 “페이지 경계들”도 검사해야 한다.

## 예시 상황 (숫자 넣기)

### 케이스 A: NULL 포인터

```text
buffer = 0x0
```

`NULL`은 커널이 그대로 역참조하면 즉시 터진다. 그래서 가장 먼저 `addr == NULL`을 막는다.

### 케이스 B: 커널 주소 침범

PintOS KAIST 코드에서 커널 가상 주소 시작점은 `KERN_BASE = LOADER_KERN_BASE`다.

- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/loader.h`
  - `#define LOADER_KERN_BASE 0x8004000000`
- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/vaddr.h`
  - `#define KERN_BASE LOADER_KERN_BASE`
  - `#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)`
  - `#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))`

그래서 다음은 유저 주소가 아니다:

```text
addr = 0x8004000000  (커널 베이스)
```

### 케이스 C: 버퍼가 페이지 경계를 넘어감

페이지 크기 `PGSIZE = 0x1000`(4096)이라고 하자.

```text
buffer = 0x473fffe0
size   = 0x40 (64 bytes)

buffer + size - 1 = 0x4740001f
```

이 경우 `buffer`는 0x473ff000 페이지인데, 끝 주소는 0x47400000 페이지로 넘어간다.

그래서 “처음만 검사”하면 안 되고, **끝 주소**도 검사해야 한다.

## Linux / Windows에서는 (현실 기준)

현실 OS에서는 “검사”가 보통 더 정교하다.

- Linux는 `copy_from_user()`, `copy_to_user()` 같은 “유저 메모리 복사 유틸리티”를 통해 커널이 안전하게 접근한다.
  - 주소 범위만 보는 게 아니라, 실제로 복사하면서 fault를 처리하거나 에러로 되돌린다.
  - 스펙(ABI, 보안, 성능, 동시성)이 커서 PintOS보다 훨씬 복잡하다.
- Windows도 유저 주소 공간 접근은 별도의 안전한 경로로 다루며, 유저 포인터는 항상 “검증/예외 처리 대상”이다.

핵심은 동일하다:

> 커널은 유저 포인터를 “데이터”가 아니라 “공격 가능 입력”으로 취급한다.

## PintOS에서는 (코드로 내려가기)

### 1) 단일 주소 검사: `check_address()`

PintOS(이 코드베이스)에서는 syscall 구현 안에서 유저 주소 검사를 직접 호출한다.

- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`
  - `check_address(const void *addr)`

이 함수가 하는 일은 “2단계 모델” 그대로다.

1) `addr == NULL`이면 종료
2) `is_user_vaddr(addr)`가 아니면 종료(커널 영역 침범 차단)
3) `pml4_get_page(curr->pml4, addr)`가 NULL이면 종료(매핑이 없음)

여기서 `pml4_get_page`는 페이지 테이블 워킹을 한다.

- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/mmu.c`
  - `pml4_get_page(uint64_t *pml4, const void *uaddr)`

중요한 관찰:

- PintOS(프로젝트2)는 “매핑이 없으면 종료”로 단순하게 처리한다.
- 프로젝트3(VM)에서는 lazy loading 같은 이유로 “지금은 매핑이 없어도 나중에 fault에서 처리”할 수 있어, 이 정책이 그대로면 오히려 과하게 죽을 수 있다.
  - 즉, **PintOS의 포인터 검증은 과제 단계에 따라 의미가 달라진다.**

### 2) 버퍼 검사: `check_user_buffer()`

- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`
  - `check_user_buffer(const void *buffer, unsigned size)`

이 함수는 다음을 확인한다.

- `buffer` 자체 검사
- `buffer + size - 1` 검사
- 그리고 `pg_round_down()`으로 페이지 경계를 따라가며 **해당 구간에 포함되는 모든 페이지 시작 주소**를 검사

“페이지 경계 단위로 검사”가 핵심이다.

### 3) 문자열 검사: `check_user_string()`

- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`
  - `check_user_string(const char *str)`

문자열은 길이를 모른다. 그래서 `'\0'`을 만날 때까지 1바이트씩 전진하며 매 바이트 주소를 검사한다.

## QEMU에서는 (하드웨어 역할)

QEMU는 “포인터가 유효한지”를 판단하지 않는다.

그 대신 QEMU는 CPU의 규칙을 흉내 낸다:

- guest가 어떤 가상 주소(gva)에 접근한다
- MMU 변환이 실패하면 CPU는 #PF(Page Fault)를 발생시키고
- fault 주소는 CR2에 저장된다

QEMU 코드에서 이 패턴을 “증거로” 볼 수 있다.

- QEMU: `/Users/woonyong/workspace/Krafton-Jungle/QEMU/target/i386/emulate/x86_mmu.c`
  - 변환 실패 시 `env->cr[2] = gva;`
  - 그리고 page fault 예외(`EXCP0E_PAGE`)를 raise 한다

PintOS에서 CR2를 읽는 코드와 정확히 맞물린다.

- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/exception.c`
  - `fault_addr = (void *) rcr2();`
  - `printf ("Page fault at %p: ...", fault_addr, ...)`

즉:

- “fault_addr가 어디서 오나?” → QEMU가 CPU의 CR2 동작을 재현해서 넣어준다.
- “fault를 어떻게 처리하나?” → PintOS의 `page_fault()`가 결정한다.

## 차이점 (혼동 방지)

| 항목 | Linux / Windows | PintOS | QEMU |
|---|---|---|---|
| 유저 포인터 취급 | 커널 API로 안전하게 copy | 과제 코드에서 직접 검사/종료 | 포인터 의미 모름(하드웨어 동작만 재현) |
| 매핑이 없을 때 | fault/에러 처리 + VM 정책 | project2: 보통 즉시 종료 | #PF 예외/CR2 세팅 |
| “누가 죽나?” | 보통 유저 프로세스만 종료 | 과제 요구에 따라 exit(-1) | QEMU는 guest가 낸 예외를 전달 |

## 직접 확인 (GDB / 테스트로 확인)

### 0) 테스트로 빠르게 확인

PintOS userprog 테스트에는 “유저 포인터가 잘못됐을 때 커널이 죽지 않고 프로세스만 종료(exit(-1))해야 한다”를 확인하는 케이스들이 있다.

- PintOS tests: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/tests/userprog/`
  - `write-bad-ptr`, `read-bad-ptr`, `open-null`, `create-null`, `exec-bad-ptr`
  - `*-boundary` 계열: 포인터가 페이지 경계를 넘을 때를 노린 케이스

1) syscall에서 유저 포인터가 검사되는지
   - breakpoint: `syscall_handler`, `check_address`, `check_user_buffer`, `check_user_string`
   - `p/x f->R.rsi` (예: write의 buffer)

2) 고의로 invalid pointer를 넘겨서 #PF가 나는지
   - `Page fault at ...` 로그가 찍히는지 확인
   - GDB에서 `info registers cr2`로 fault 주소가 같은지 확인

3) “페이지 경계 넘어가는 케이스”를 만들기
   - `buffer`를 페이지 끝 근처로 잡고 size를 늘려서 두 페이지를 건너게 만들기

## 다음으로 볼 문서

- [[syscall-end-to-end]]: syscall 전체 흐름에서 이 검증이 어디에 끼는지
- [[address-translation-memory]]: “가상 주소 → 물리 주소” 변환이 왜 실패하는지
- (추가 예정) `page-fault-trace`: #PF를 “주소 변환 실패 → handler”로 끝까지 Trace
