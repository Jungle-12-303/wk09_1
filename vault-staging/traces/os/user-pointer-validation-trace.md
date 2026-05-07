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
  - layer:memory
  - topic:syscall
  - topic:page-table
  - topic:gdb
related_to:
  - "[[week-2-user-programs-map]]"
  - "[[syscall-end-to-end]]"
  - "[[address-translation-memory]]"
  - "[[concept-to-code-map]]"
---

# 유저 포인터 검증 (User Pointer Validation) Trace

## 작은 질문

유저 프로그램이 커널에 `buffer` 포인터를 넘기면(예: `write(fd, buffer, size)`), 커널은 그 포인터를 그냥 믿고 `memcpy`처럼 읽어도 될까?

정답은 **안 된다**다. 유저가 넘긴 포인터는 “커널이 접근해도 안전한 메모리”라는 보장이 전혀 없다.

이 Trace는 아래 두 질문을 끝까지 내려가서 연결한다.

- “안전하지 않다”는 말은 구체적으로 무엇이 위험한가?
- PintOS에서는 그 위험을 어떤 코드로 막고, QEMU는 어떤 하드웨어 효과(#PF)를 흉내 내는가?

## 왜 필요한가

유저 포인터를 검증하지 않으면 커널은 두 가지를 잃는다.

1) **안정성**: 매핑되지 않은 주소를 읽다 커널이 예외를 맞고 죽을 수 있다.
2) **보안/격리**: 유저가 커널 주소(혹은 다른 프로세스의 메모리처럼 보이는 주소)를 넘겨 커널이 대신 읽게 만들 수 있다.

즉 “유저 포인터 검증”은 *예의 바른 입력 검증*이 아니라 **커널의 생존과 격리**를 위한 방어선이다.

## 핵심 모델 (머릿속에 넣을 최소 모델)

유저가 넘긴 포인터 `p`를 “안전하다”고 말하려면 최소 두 가지가 성립해야 한다.

1) **범위(range)**: `p`가 유저 주소 공간에 속한다. (커널 영역 침범 금지)
2) **매핑(mapping)**: 현재 프로세스의 페이지 테이블에서 `p`가 실제 물리 프레임으로 매핑되어 있다.

그리고 `size`가 붙으면 한 가지가 더 붙는다.

3) **구간(range length)**: `[p, p+size)`가 페이지 경계를 넘더라도, 구간 전체가 매핑되어 있다.

## 예시 상황 (실제로 어디서 터지나?)

### 예시 1) `NULL` 포인터

```c
write (1, NULL, 5);
```

- `NULL`은 “아무 것도 가리키지 않는다”.
- 커널이 이 주소를 그대로 읽으려 하면 바로 실패해야 한다.

### 예시 2) 커널 주소를 넘기는 공격/실수

```c
write (1, (void *) 0xffff800000000000, 5);
```

- 이 값이 의미하는 바는 “유저 코드가 커널 영역을 가리키는 포인터를 만들었다”는 것뿐이다.
- 커널이 이것을 허용하면 “유저가 커널 메모리를 읽게 만들기” 같은 사고로 이어질 수 있다.

### 예시 3) 버퍼가 페이지 경계를 넘는 경우

`PGSIZE = 0x1000`일 때, 아래 버퍼는 2개의 페이지에 걸친다.

```text
buffer = 0x8048ff0
size   = 0x30

[0x8048ff0, 0x8049020)  # 페이지 경계(0x8049000)를 넘는다
```

이때 “첫 주소만 매핑되어 있으면 OK”가 아니다. **구간 전체가 매핑되어 있어야** 한다.

## Linux / Windows에서는 (현실 기준으로 잡기)

현실 OS는 보통 “커널이 유저 포인터를 직접 역참조하지 않게” 강제한다.

- Linux: 커널은 유저 메모리를 `copy_from_user()`/`copy_to_user()` 같은 경로로 복사하며, 접근 중 fault가 나도 커널이 죽지 않도록 예외 처리 경로를 가진다.
- Windows: 커널/드라이버는 유저 버퍼를 직접 믿지 않고(필요 시 probe/예외 처리), 잘못된 유저 주소 접근을 오류로 처리한다.

PintOS 과제는 이 복잡한 안전장치를 “학습 가능한 크기”로 축소해 직접 구현하게 만든다.

## PintOS에서는 (코드로 내려가기)

### 1) syscall 인자(buffer)가 어디서 들어오나?

PintOS KAIST에서는 syscall 핸들러가 레지스터에서 인자를 꺼낸다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`
  - `SYS_WRITE`에서 `write(f->R.rdi, (const void *) f->R.rsi, f->R.rdx)` 호출

여기서 `f->R.rsi`가 **유저가 커널에 넘긴 가상 주소**다.

### 2) write()가 버퍼를 어떻게 검증하나?

같은 파일의 `write()`는 바로 `check_user_buffer(buffer, size)`를 호출한다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`

핵심 구조:

```c
check_user_buffer (buffer, size);
...
putbuf (buffer, size);
```

즉 PintOS는 “검증 → 사용” 순서를 명시적으로 둔다.

### 3) check_address(): 무엇을 확인하나?

`check_address()`는 “이 주소가 유저 공간이고, 현재 프로세스의 페이지 테이블에서 매핑되어 있는가?”를 확인한다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`

핵심 조건:

```c
if (addr == NULL) exit (-1);
if (!is_user_vaddr (addr)) exit (-1);
if (pml4_get_page (curr->pml4, addr) == NULL) exit (-1);
```

여기서 `is_user_vaddr()`는 유저/커널 주소 범위를 나누는 매크로다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/vaddr.h`
  - `#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))`

범위 기준이 되는 커널 베이스 값은 다음처럼 잡힌다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/loader.h`
  - `#define LOADER_KERN_BASE 0x8004000000`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/vaddr.h`
  - `#define KERN_BASE LOADER_KERN_BASE`

즉, (이 코드베이스에서) 대략 이렇게 이해하면 된다.

```text
addr <  0x8004000000  -> 유저 주소 후보
addr >= 0x8004000000  -> 커널 주소 (유저가 넘기면 즉시 종료해야 함)
```

그리고 `pml4_get_page()`는 “페이지 테이블을 걸어서 present한 매핑이 있는지”를 확인한다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/mmu.c`
  - `pml4_get_page(pml4, uaddr)`는 PTE가 present면 커널 VA(=해당 물리 프레임을 바라보는 주소)를 돌려준다.

주의: 프로젝트 3(VM)로 가면, “지금 당장 present 매핑이 없다”가 “곧바로 exit(-1) 해야 한다”와 같지 않을 수 있다.

- demand paging/lazy loading에서는, syscall이 유저 버퍼를 만지는 순간에 fault가 나고 그 fault를 커널이 처리해 페이지를 가져올 수도 있다.
- 즉, project2의 단순한 `pml4_get_page == NULL -> exit(-1)` 정책은 project3에서는 재검토 대상이 된다.

### 4) check_user_buffer(): “구간 전체”를 어떻게 다루나?

`check_user_buffer()`는 버퍼가 페이지 경계를 넘을 수 있다는 전제를 깔고, 페이지 단위로 검증한다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`

핵심 아이디어:

```c
if (size == 0)
    return;
check_address (buffer);
check_address (addr + size - 1);
for (start = pg_round_down (addr); start <= pg_round_down (addr + size - 1); start += PGSIZE)
    check_address ((const void *) start);
```

`size == 0`이면 검증할 바이트가 없으므로 바로 돌아간다. 이 분기가 없으면 `addr + size - 1`이 `addr - 1`처럼 계산되어, 빈 버퍼인데도 엉뚱한 주소를 검사할 수 있다.

숫자를 넣으면 루프의 의미가 더 분명해진다.

```text
buffer = 0x8048ff0
size   = 0x30
last   = buffer + size - 1 = 0x804901f

pg_round_down(buffer) = 0x8048000
pg_round_down(last)   = 0x8049000
```

따라서 이 버퍼는 실제로 두 페이지를 건드린다.

```text
check_address(0x8048ff0)   # 시작 바이트
check_address(0x804901f)   # 마지막 바이트
check_address(0x8048000)   # 첫 번째 page base
check_address(0x8049000)   # 두 번째 page base
```

이 루프는 “버퍼가 걸치는 모든 페이지”를 한 번씩 찍어 보며 매핑을 확인한다.

### 5) check_user_string(): 길이를 모르는 문자열은 어떻게 검증하나?

문자열(`char *`)은 `size`가 없다. 그래서 `'\0'`을 만날 때까지 한 글자씩 전진하며 “그 바이트를 읽어도 안전한가?”를 반복해서 확인하는 형태가 된다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`
  - `check_user_string(const char *str)`

이 검증이 필요한 대표적인 syscall 인자는 “파일 이름”이나 “실행할 커맨드라인 문자열” 같은 것들이다.

## QEMU에서는 (하드웨어 효과: #PF를 어떻게 만들까?)

QEMU는 “유저 포인터를 검증하는 정책”을 제공하지 않는다. 그건 guest OS(PintOS)의 책임이다.

하지만 **잘못된 주소 접근이 일으켜야 하는 CPU 예외(#PF)** 는 QEMU가 guest CPU의 동작처럼 재현해야 한다.

예를 들어 guest가 어떤 가상 주소 `gva`를 읽으려는데 주소 변환이 실패하면, QEMU는 페이지 폴트 예외를 일으킨다.

- `/Users/woonyong/workspace/Krafton-Jungle/QEMU/target/i386/emulate/x86_mmu.c`

핵심 코드(읽기 경로):

```c
translate_res = mmu_gva_to_gpa(cpu, gva, &gpa, translate_flags);
if (translate_res) {
    int error_code = translate_res_to_error_code(translate_res, false, is_user(cpu));
    env->cr[2] = gva;
    x86_emul_raise_exception(env, EXCP0E_PAGE, error_code);
    return translate_res;
}
```

이 코드가 말해주는 것:

- “가상 주소 → 물리 주소” 변환이 실패하면,
- `CR2`에 fault 주소를 넣고,
- `#PF(Page Fault)` 예외를 올린다.

즉 PintOS가 `check_address()`를 빼먹고 커널에서 유저 포인터를 잘못 접근하면, 결국 이쪽 경로를 타서 guest는 #PF를 맞게 된다.

## 차이점 (현실 vs 과제 vs 에뮬레이터)

| 항목 | Linux / Windows | PintOS | QEMU |
|---|---|---|---|
| “유저 포인터 안전 접근” 방식 | 커널 내부 안전 복사 + 예외 처리 | syscall에서 직접 검사 후 종료(과제 구현) | 정책 없음. 주소 변환 실패 시 #PF를 에뮬레이션 |
| 실패 시 결과 | 보통 `-EFAULT` 같은 오류/예외 | `exit(-1)`로 프로세스 종료(과제 요구에 맞춰 단순화될 수 있음) | guest에 `#PF` 전달 |
| 핵심 자료구조 | 프로세스 page table + 권한 모델 | `pml4` + `pml4_get_page()` | guest CPU state + MMU 변환 코드 |

## 직접 확인 (GDB로 “검증 → 사용”을 눈으로 보기)

### 1) 검증이 호출되는지

- breakpoint: `check_user_buffer`, `check_address`
- 확인: `p/x buffer`, `p size`

### 2) range 체크가 막는 커널 주소

- `check_address`에서 `p addr`
- `p is_user_vaddr(addr)`가 false가 되는 케이스를 만들어 본다.

### 3) mapping 체크가 막는 unmapped 주소

- `check_address`에서 `p curr->pml4`
- `p pml4_get_page(curr->pml4, addr)`가 `0x0`인지 확인

### 4) page fault와 연결하기

검증을 일부러 빼거나(실험용 브랜치), 혹은 검증이 커버하지 못하는 케이스를 만들면 #PF가 발생한다.

- breakpoint: `page_fault`
  - `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/exception.c`
- 확인: `p/x rcr2()` 또는 `p fault_addr` (코드에 따라)
  - 추가 확인: GDB에서 `info registers cr2`로 fault 주소가 같은지 보기

## PintOS에서 “검증이 필요한 테스트” 힌트

PintOS 기본 테스트 중에도 유저 포인터 검증을 직접 때리는 케이스가 있다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/tests/userprog/bad-read.c`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/tests/userprog/bad-write.c`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/tests/userprog/open-bad-ptr.c`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/tests/userprog/read-bad-ptr.c`
- `*-boundary` 계열(`read-boundary`, `write-boundary`, `exec-boundary` 등): 페이지 경계를 넘는 포인터를 노린 케이스

이 테스트들은 “유저가 일부러 이상한 포인터를 넘겼을 때 커널이 죽지 않고 프로세스를 종료/처리하는가?”를 확인한다.

## 다음으로 볼 문서

- [[syscall-end-to-end]]: syscall 인자가 레지스터에서 들어오는 전체 흐름
- [[file-descriptor-knowledge]]: `write(fd, buffer, size)`에서 fd가 무엇을 가리키는지
- [[address-translation-memory]]: “매핑되어 있다”를 page table 관점으로 다시 보기
- [[week-2-user-programs-map]]: 2주차 전체 링크 허브
