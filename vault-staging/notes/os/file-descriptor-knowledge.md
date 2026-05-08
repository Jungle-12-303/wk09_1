---
type: Knowledge
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
  - topic:fd
  - topic:syscall
related_to:
  - "[[week-2-user-programs-map]]"
  - "[[syscall-end-to-end]]"
  - "[[user-pointer-validation-trace]]"
---
# 파일 디스크립터(fd)와 Windows handle 모델

## 목차

- 작은 질문
- 왜 필요한가
- 핵심 모델
- Linux / Windows에서는
- PintOS에서는
- QEMU에서는
- 숫자와 메모리
- 직접 확인
- 다음으로 볼 문서
## 작은 질문

`write(1, "hello", 5)`에서 `1`은 왜 “파일 포인터”가 아니라 작은 정수일까?

그리고 Windows에서는 왜 `WriteFile(handle, ...)`처럼 “handle”이라는 다른 단어를 쓸까?

## 왜 필요한가

유저 프로그램이 커널의 파일/장치 같은 자원을 쓰려면, 커널이 **“누가 무엇을 어떤 권한으로 쓰는지”**를 강제로 통제해야 한다.

만약 유저 프로그램이 커널 내부 객체(예: `struct file *`)를 직접 가리키는 포인터를 넘길 수 있다면:

- 유저가 “커널 메모리 주소를 위조”해서 다른 프로세스의 파일 객체를 가리키는 척할 수 있고
- 커널이 그 포인터를 믿고 역참조하면 커널 크래시 또는 권한 상승으로 이어질 수 있다

그래서 현실 OS는 유저에게 **커널 객체를 직접 노출하지 않고**, 대신 “의미 없는 작은 숫자(토큰)”를 준다. 그게 Linux의 fd, Windows의 handle이다.

## 핵심 모델

fd/handle은 본질적으로 다음 구조다.

```text
user-visible integer (fd/handle)
  -> (kernel-owned table lookup)
  -> kernel object pointer + access rights
```

이 말은 즉:

- 유저는 숫자만 가진다
- 커널만 “숫자 → 실제 객체” 매핑표를 가진다
- 커널은 매 요청마다 “이 숫자가 이 프로세스 소유인지, 범위가 맞는지, 권한이 맞는지”를 검증한다

## 예시 상황 (숫자 감각 만들기)

아래 호출이 있다고 하자.

```c
int fd = open ("a.txt");  // 예: fd == 2
write (fd, "hi", 2);
close (fd);
```

유저가 가진 건 `2`라는 정수뿐이다. “2가 어떤 파일인지”는 커널이 가진 테이블에서만 알 수 있다.

## Linux / Windows에서는

### Linux: fd 모델(프로세스의 file descriptor table)

- fd는 프로세스 단위로 관리되는 “정수 인덱스”다. (일반적으로 0,1,2는 stdin/stdout/stderr)
- 커널은 fd를 `struct file` 같은 커널 객체로 매핑해 관리한다.
- `fork()`에서는 **fd 숫자가 그대로 복사**되며(보통) 같은 “열린 파일 상태(오프셋 등)”를 공유하는 형태가 된다.

초보자가 기억할 결론:

- fd는 “파일 그 자체”가 아니라 “열린 파일을 가리키는 표의 번호”다.

### Windows: handle 모델(프로세스의 handle table + 권한)

- handle도 유저에겐 정수처럼 보이지만, 커널 오브젝트(파일/프로세스/스레드/이벤트 등)를 가리키는 “토큰”이다.
- handle에는 접근 권한(access right) 개념이 강하게 결합되어 있다.

초보자가 기억할 결론:

- Windows는 “파일만 fd로 다룬다”기보다, 여러 커널 자원을 “handle 하나의 추상”으로 묶는 쪽에 가깝다.

## PintOS에서는

PintOS는 교육을 위해 fd를 아주 단순화한다.

### 핵심 구현 아이디어: fd_table = 포인터 배열

PintOS는 현재 스레드(`struct thread`)에 다음 필드를 둔다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/thread.h`
  - `struct file **fd_table`
  - `int next_fd`

그리고 fd_table의 최대 슬롯 수를 “한 페이지(4KB)를 포인터 배열로 쓰는 방식”으로 정한다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/process.c`
  - `#define FD_MAX (PGSIZE / sizeof (struct file *))`

`open()`이 호출되면 다음이 일어난다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`
  - `opened_file = filesys_open(file)`
  - `fd = process_add_file(opened_file)`

`process_add_file()`는 “비어 있는 슬롯”을 찾아 그 인덱스를 fd로 반환한다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/process.c`
  - `for (fd = 2; fd < FD_MAX; fd++) ...`

여기서 `fd = 2`부터 시작하는 이유는 PintOS가 `0/1`을 stdin/stdout 용도로 예약하기 때문이다.

### fork에서의 fd 동작(현실 OS와의 차이 포인트)

PintOS의 `fork()`는 fd_table을 그대로 공유하지 않고, 각 엔트리를 `file_duplicate()`로 복제한다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/process.c`
  - `duplicate_fd_table()` → `file_duplicate(parent->fd_table[fd])`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/filesys/file.c`
  - `file_duplicate()`는 inode를 reopen하고 `pos`를 복사한다

이 설계가 의미하는 것(초보자 관점 결론):

- “부모와 자식이 같은 파일을 열었지만”, file 객체의 `pos`는 서로 독립적으로 움직일 수 있다.
- 현실 Linux의 `fork()`에서 보통 기대하는 “파일 오프셋 공유”와 다를 수 있다.

## QEMU에서는

QEMU는 fd/handle 개념을 구현하지 않는다.

QEMU는 PintOS 커널 코드가 실행될 수 있도록:

- guest CPU의 레지스터/특권 전환
- guest 메모리
- 장치(예: 디스크, 타이머)와 interrupt

를 에뮬레이션할 뿐이다.

“fd=2가 어떤 파일을 의미하는지”는 PintOS 커널이 결정한다.

## 차이점 (핵심만)

| 항목 | Linux | Windows | PintOS | QEMU |
|---|---|---|---|---|
| 유저에게 보이는 값 | fd(정수) | handle(토큰) | fd(정수) | 해당 없음 |
| 커널 매핑표 | per-process fd table | per-process handle table | per-thread(프로세스) fd_table | 해당 없음 |
| 권한 모델 | fd+권한(간접) | handle에 권한 강결합 | 단순화(과제 범위 중심) | 해당 없음 |
| fork 시 동작 | 보통 열린 파일 상태 공유 | 프로세스 생성 방식이 다름 | `file_duplicate()`로 복제 | 해당 없음 |

## 코드 증거 (PintOS)

- fd_table 구조: `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/thread.h`
- fd 할당/조회/반납:
  - `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/process.c`
    - `process_add_file`
    - `process_get_file`
    - `process_close_file`
- syscall이 fd를 쓰는 방식:
  - `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/syscall.c`
    - `open`, `read`, `write`, `close`
- fork가 fd_table을 복제하는 방식:
  - `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/userprog/process.c`
    - `duplicate_fd_table`

## 숫자와 메모리 (fd_table을 “바이트”로 보기)

PintOS는 `FD_MAX = PGSIZE / sizeof (struct file *)`다.

교육용 x86-64 환경에서 보통:

```text
PGSIZE           = 4096 bytes
sizeof(pointer)  = 8 bytes
FD_MAX           = 4096 / 8 = 512
```

이 말은 즉:

- fd_table은 512개의 포인터 슬롯을 가진 배열이다.
- `fd=2..511` 중 비어 있는 슬롯을 찾아 할당한다.

메모리 오프셋 관점에서, `fd_table`의 시작 주소를 `BASE`라고 하면:

```text
fd_table[fd] 주소 = BASE + (fd * 8)
```

예를 들어 `fd=3`이면 `BASE + 24` 바이트 위치에 “struct file * 포인터 값”이 저장된다.

## 직접 확인 (디버깅 체크리스트)

다음을 한 번이라도 직접 확인하면 fd가 “정수 ↔ 테이블 ↔ 포인터”라는 감각이 생긴다.

1. `open()` 직후 fd 확인
   - breakpoint: `open` 또는 `process_add_file`
   - `p fd`
2. fd_table 내용 확인
   - `p thread_current()->fd_table`
   - `p/x thread_current()->fd_table[fd]`
3. 같은 fd로 `write()`가 파일 객체를 찾아가는지 확인
   - breakpoint: `process_get_file`, `file_write`

## 꼬리에 꼬리를 무는 질문

- 왜 fd/handle은 포인터가 아니라 정수(토큰)인가?
- 왜 `0/1/2`는 관습적으로 예약되었나? (PintOS는 왜 `2`부터 할당하나?)
- `fork()`에서 “파일 오프셋 공유”가 되면 어떤 버그/장점이 생기나?
- PintOS의 `file_duplicate()`는 어떤 의미에서 현실 OS를 단순화했나?
- QEMU는 fd를 “모르는데도” 왜 syscall 흐름이 돌아가나?

## 다음으로 볼 문서

- [[syscall-end-to-end]]: syscall ABI 관점에서 fd가 레지스터 인자로 들어가는 흐름
- [[user-pointer-validation-trace]]: fd 자체보다 위험한 건 “유저 포인터”라는 점
