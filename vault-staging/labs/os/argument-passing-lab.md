---
type: Lab
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
  - topic:process
  - topic:syscall
  - topic:register
related_to:
  - "[[week-2-user-programs-map]]"
  - "[[syscall-end-to-end]]"
  - "[[cpu-register-execution]]"
---

# 인자 전달 (Argument Passing) Lab — argv가 스택에 쌓이는 바이트

## 작은 질문

`/bin/echo x y` 같은 프로그램을 실행할 때, `main(int argc, char **argv)`의 `argv[0]`, `argv[1]`은 도대체 어디에서 “생기는” 걸까?

답: 커널이 유저 스택에 문자열과 포인터 배열을 직접 **바이트 단위로 쌓아 준 결과**다.

이 Lab의 목표는 “argv는 추상 배열”이 아니라 **유저 스택 위의 바이트 레이아웃**이라는 감각을 만드는 것이다.

## 왜 필요한가

User Programs 과제에서 흔히 막히는 지점은 이거다.

- “문자열을 어디에 복사해야 하지?”
- “argv 포인터는 어떤 주소를 가리켜야 하지?”
- “정렬(alignment)은 왜 맞춰야 하지?”
- “레지스터로 argc/argv를 전달한다는 말이 무슨 뜻이지?”

이 Lab을 끝내면, 디버깅할 때 “스택 덤프를 보고 직접 판단”할 수 있게 된다.

## 핵심 모델

프로세스 시작 시점에는 다음 3가지가 필요하다.

1) 유저 스택에 실제 문자열 바이트가 있어야 한다.
2) `argv[i]`는 그 문자열의 시작 주소를 가리켜야 한다.
3) 시작 레지스터/스택 포인터가 ABI(프로젝트 규칙)에 맞게 설정되어야 한다.

## 실제 OS에서는 (Linux / Windows)

Linux(x86-64)에서는 프로세스 시작 시 스택에 다음이 올라간다(개념 요약).

- `argc`
- `argv[]` 포인터들 + NULL
- `envp[]` 포인터들 + NULL
- `auxv`(보조 벡터) 등

그리고 유저 공간 런타임이 `_start`에서 `main(argc, argv, envp)`로 연결한다.

Windows는 시작점이 다르고(PE 로딩, PEB/TEB 등), Win32 CRT가 커맨드라인 파싱을 다시 하는 등 모델이 다르다.

하지만 둘 다 “유저 스택에 인자 정보가 배치된다”는 점은 공통이다.

## PintOS에서는 (코드로 내려가기)

이 코드베이스의 인자 전달은 `load()` 안에서 이뤄진다.

- PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/process.c`
  - `load(const char *file_name, struct intr_frame *if_)`
  - `setup_stack(if_)`로 유저 스택 페이지를 만들고
  - `if_->rsp`를 기준으로 아래 방향으로 문자열들을 복사한 뒤
  - 포인터 배열을 쌓고
  - 시작 레지스터 `if_->R.rdi`/`if_->R.rsi`를 설정한다

PintOS가 만드는 시작 레지스터:

- `if_->R.rdi = argc`
- `if_->R.rsi = argv 배열 시작 주소`
- `if_->rsp = 최종 스택 포인터`

즉 PintOS는 “argc/argv를 스택이 아니라 레지스터로 전달”하는 형태로 단순화되어 있다.

## 실험: "echo x y"를 바이트로 쌓아보기

가정:

- 유저 스택 top: `USER_STACK = 0x47480000`
  - PintOS: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/vaddr.h`

실행 문자열:

```text
argv[0] = "echo" (5 bytes, including '\0')
argv[1] = "x"    (2 bytes)
argv[2] = "y"    (2 bytes)
argc    = 3
```

### 1) 문자열을 역순으로 복사

PintOS 코드처럼 “마지막 인자부터” 스택 아래로 복사한다고 하자.

초기:

```text
stack_p = 0x47480000
```

"y\0"(2 bytes):

```text
stack_p = 0x4747fffe
memory  [0x4747fffe..] = 79 00
argv[2] = 0x4747fffe
```

"x\0"(2 bytes):

```text
stack_p = 0x4747fffc
memory  [0x4747fffc..] = 78 00
argv[1] = 0x4747fffc
```

"echo\0"(5 bytes):

```text
stack_p = 0x4747fff7
memory  [0x4747fff7..] = 65 63 68 6f 00
argv[0] = 0x4747fff7
```

여기까지는 “문자열 바이트”만 쌓였다.

### 2) 8바이트 정렬(alignment) 맞추기

x86-64에서 포인터는 8바이트 단위다. PintOS 코드는:

```text
stack_p = (uintptr_t)stack_p & -8
```

예를 들어 `0x4747fff7`은 8로 나누어 떨어지지 않으므로, 정렬 후:

```text
stack_p = 0x4747fff0
```

이 사이의 바이트(0x4747fff0~0x4747fff6)는 “패딩”으로 남는다.

### 3) argv[argc] = NULL 자리

```text
stack_p -= 8
*(uint64_t*)stack_p = 0
```

이제 `stack_p`가 가리키는 8바이트는 NULL 포인터다.

### 4) argv 포인터들을 역순으로 push

PintOS 코드는 각 `argv[i]`(문자열 주소)를 8바이트로 스택에 쌓는다.

```text
push argv[2] = 0x4747fffe
push argv[1] = 0x4747fffc
push argv[0] = 0x4747fff7
```

이제 “argv 배열 시작 주소”는 현재 `stack_p`가 된다.

### 5) 시작 레지스터/스택 설정

PintOS는 다음처럼 시작 상태를 만든다.

- `if_->R.rdi = argc` (3)
- `if_->R.rsi = argv 배열 시작 주소`
- `if_->rsp = (argv 배열 아래) 추가 8바이트 0을 둔 뒤의 값`

정리하면:

- 문자열은 스택 위쪽에 바이트로 존재하고
- argv는 그 문자열들을 가리키는 “포인터 배열”이며
- 커널은 최종적으로 `RDI/RSi/RSP`를 맞춰서 유저 코드가 자연스럽게 시작되게 한다

## QEMU에서는 (역할 분리)

QEMU는 argv를 만들어 주지 않는다.

- argv 레이아웃을 만드는 건 PintOS(guest OS) 커널 코드다.
- QEMU는 그 바이트들이 실제로 스택 메모리에 저장되고, 이후 유저 코드가 그 주소를 읽을 때 정상 동작하게 CPU/MMU를 에뮬레이션한다.

## 직접 확인 (디버깅)

### 0) 테스트로 빠르게 확인

PintOS userprog 테스트에는 인자 전달을 직접 깨는 케이스가 포함되어 있다.

- PintOS tests: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/tests/userprog/args-*.ck`
  - `args-none`, `args-single`, `args-multiple`, `args-many`, `args-dbl-space`

1) `load()` 안에서 스택 빌드 직전에 breakpoint
   - breakpoint 후보: `load`(process.c)에서 문자열 복사 루프 직후

2) 최종 `if_->rsp`, `if_->R.rsi`를 출력
   - `p/x if_->rsp`
   - `p/x if_->R.rsi`
   - `p if_->R.rdi`

3) 스택 덤프로 argv 확인
   - `x/32gx if_->rsp`
   - `x/s *(char**)if_->R.rsi` (argv[0] 문자열)

## 정리

`argv`는 “마법 배열”이 아니라, 커널이 유저 스택에 만든 **포인터 배열 + 문자열 바이트**다.

이 감각이 잡히면 다음이 쉬워진다.

- syscall에서 유저 포인터 검증: [[user-pointer-validation-trace]]
- page fault가 났을 때 “어떤 주소를 잘못 접근했는지” 판단하기

## 다음으로 볼 문서

- [[syscall-end-to-end]]
- [[user-pointer-validation-trace]]
- [[address-translation-memory]]
