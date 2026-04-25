---

## name: c-style-pintos description: PintOS 커널 C 코딩 스타일 컨벤션. PintOS 프로젝트의 .c / .h 파일을 작성·리뷰·리팩터링할 때 활성화. "PintOS", "thread.c", "timer.c", "synch.c", "lock", "semaphore", "thread", "scheduler" 같은 PintOS 용어가 언급되거나 PintOS 소스를 생성·수정할 때 이 스킬을 따른다. 일반 C 컨벤션 ([c-style.md](http://c-style.md), Linux kernel 변형) 과는 별개로 PintOS 의 GNU/스탠퍼드 스타일을 따른다.

# PintOS C 코딩 스타일 컨벤션

> 목적: PintOS 의 기존 코드 스타일과 일관된 신규 코드를 작성한다. 일반 팀 C 컨벤션 (`c-style.md`) 은 4-space + `func()` 공백 없음 스타일이지만, **PintOS 는 TAB +** `func ()` **공백 있음** 스타일이다. 두 컨벤션을 혼용하지 않는다.
>
> 기반: GNU C 스타일 + Stanford CS 140 (PintOS 원작) 관행. 적용 대상: `pintos/` 디렉터리 안의 모든 `.c`, `.h` 파일.

---

## 0. 한눈에 보는 핵심 차이 — 일반 컨벤션 vs PintOS

항목일반 ([c-style.md](http://c-style.md))PintOS인덴트4 space**TAB** (보통 8-column 표시, IDE 에서 4 로 보일 수 있음)함수 호출 괄호`func()func ()` (공백)함수 정의 brace다음 줄**같은 줄**제어문 brace같은 줄같은 줄주석 다중행`/* ... */` 자유`/* line1\n line2 */` (텍스트 정렬)변수 선언 위치사용 직전함수 시작 (C89 관행)헤더 가드`MODULE_FILE_H`동일

PintOS 코드를 수정할 때 무조건 PintOS 스타일을 따른다. 새 파일도 마찬가지.

---

## 1. 인덴트 — TAB 사용

PintOS 의 모든 .c / .h 는 **TAB 문자** 를 인덴트로 사용한다. 공백 인덴트 금지.

```c
void
thread_block (void) {
	ASSERT (!intr_context ());        /* ← 이 줄 시작은 1 TAB */
	ASSERT (intr_get_level () == INTR_OFF);

	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}
```

### 표시 폭

- TAB 의 **표시 폭은 8 칸** 이 PintOS 관행 (Stanford CS 140 시절 GNU 기본).
- 현대 IDE 에서 4 칸으로 표시해도 무관. 중요한 건 **저장된 문자가 TAB** 이라는 점.
- IDE 설정:
  - **CLion**: `Settings → Editor → Code Style → C/C++ → Tabs and Indents` 에서 `Use tab character` 체크, `Tab size: 8` 권장.
  - **VSCode**: `.editorconfig` 또는 `"editor.insertSpaces": false`.

### 혼합 금지

- 한 파일 안에서 TAB 과 SPACE 혼용 금지.
- 단, **인라인 주석 정렬용 SPACE** 는 허용 (아래 §6.3 참고).

---

## 2. 함수 정의 — 반환 타입 별도 줄

### 표준 형태

```c
void
thread_sleep (int64_t wakeup_tick) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());
	ASSERT (curr != idle_thread);

	old_level = intr_disable ();
	curr->wakeup_tick = wakeup_tick;
	list_insert_ordered (&sleep_list, &curr->elem, wakeup_tick_less, NULL);
	thread_block ();
	intr_set_level (old_level);
}
```

규칙:

1. **반환 타입은 자기 줄** 에 단독 (`void` 만 한 줄).
2. **함수명 + 인자 + opening brace 는 다음 줄**.
3. 함수명과 `(` 사이 **공백 1 칸**.
4. opening brace `{` 는 인자 닫는 `)` 와 같은 줄, 사이에 공백 1 칸.
5. 함수 본문은 1 TAB 인덴트.
6. closing brace `}` 는 0 인덴트.

### 인자가 긴 함수 — wrapping

첫 인자는 `(` 직후, 다음 인자들은 첫 인자와 정렬:

```c
static bool
wakeup_tick_less (const struct list_elem *lhs,
                  const struct list_elem *rhs,
                  void *aux UNUSED) {
	const struct thread *lhs_thread = list_entry (lhs, struct thread, elem);
	const struct thread *rhs_thread = list_entry (rhs, struct thread, elem);
	return lhs_thread->wakeup_tick < rhs_thread->wakeup_tick;
}
```

정렬 들여쓰기는 **공백 사용** (정확한 컬럼 맞추기 위해).

### static 함수

`static` 도 반환 타입 줄에 같이:

```c
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
	thread_awake (ticks);
}
```

### inline 함수 (드물지만)

```c
static inline bool
intr_context (void) {
	return in_external_intr;
}
```

### 함수 정의 사이 빈 줄

함수 정의 사이는 **빈 줄 1 줄** 이 표준. 큰 단위 그룹 사이는 2 줄도 OK.

---

## 3. 함수 선언 (prototype)

선언은 한 줄, 함수명과 `(` 사이 공백 1 칸.

```c
void thread_block (void);
void thread_unblock (struct thread *);
void thread_sleep (int64_t wakeup_tick);
void thread_awake (int64_t current_tick);

bool thread_mlfqs;                          /* 전역 변수 선언은 별도 */

tid_t thread_create (const char *name, int priority, thread_func *, void *);
```

### 인자 이름 생략

PintOS 헤더에서는 prototype 의 **인자 이름 생략** 이 흔하다 (타입만):

```c
void thread_unblock (struct thread *);    /* 이름 없음 */
void sema_init (struct semaphore *, unsigned value);  /* 일부만 명명 */
```

규칙:

- **타입만 봐도 의미가 명확하면** 이름 생략 가능.
- **여러 인자가 같은 타입** 이거나 의미가 모호하면 이름을 적는다 (예: `sema_init` 의 `value`).

### 함수 그룹화 prototype

관련 함수끼리 묶고, 그룹 사이는 빈 줄:

```c
void thread_block (void);
void thread_unblock (struct thread *);

void thread_sleep (int64_t wakeup_tick);
void thread_awake (int64_t current_tick);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);
```

---

## 4. 함수 호출

함수명과 `(` 사이 **공백 1 칸**. 인자 사이 콤마 후 공백 1 칸.

```c
list_init (&ready_list);
thread_create ("idle", PRI_MIN, idle, &idle_started);
sema_down (&sema);
list_insert_ordered (&sleep_list, &curr->elem, wakeup_tick_less, NULL);
```

### 매크로 호출도 동일

매크로도 함수처럼 공백 1 칸:

```c
ASSERT (intr_get_level () == INTR_OFF);
PANIC ("kernel bug: %s", msg);
list_entry (e, struct thread, elem);
```

### 중첩 호출

```c
thread_unblock (list_entry (list_pop_front (&ready_list), struct thread, elem));
```

각 함수마다 `(` 앞 공백 유지.

### 인자가 긴 호출

너무 길면 줄바꿈:

```c
list_insert_ordered (&sleep_list,
                     &curr->elem,
                     wakeup_tick_less,
                     NULL);
```

또는 두 줄:

```c
list_insert_ordered (&sleep_list, &curr->elem,
                     wakeup_tick_less, NULL);
```

---

## 5. 변수 선언

### C89 관행 — 함수 시작에 모음

PintOS 는 대부분 **함수 진입점에서 변수 선언** 하는 C89 관행:

```c
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;            /* 초기화 없는 변수도 OK */

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}
```

규칙:

- 함수 본문 첫 줄들은 변수 선언.
- 선언 후 빈 줄 한 칸, 그 다음 ASSERT / 코드.
- 같은 타입이면 한 줄에 여러 개도 가능: `int i, j, k;`

### C99 스타일 — 사용 직전 선언

PintOS 도 일부 코드는 사용 직전 선언 사용 (특히 for 루프):

```c
for (int i = 0; i < THREAD_CNT; i++) {
	...
}
```

신규 코드에서 짧은 변수면 사용 직전 선언 OK. 다만 함수 첫 줄에 모으는 게 PintOS 의 주류.

### 포인터 선언

`*` 는 **변수에 붙이고 타입과 떨어뜨림**:

```c
int *ptr;                /* good */
struct thread *t;        /* good */
const char *name;        /* good */

int* ptr;                /* PintOS 에서 안 씀 */
int * ptr;               /* PintOS 에서 안 씀 */
```

### const 위치

```c
const char *name;                    /* good — name 은 가변, *name 은 불변 */
const struct thread *t;              /* good — t 는 가변, *t 는 불변 */
struct thread *const t;              /* 드물게 쓰임 — t 가 불변 */
```

---

## 6. 주석

### 6.1 단일 줄 주석

```c
/* 한 줄 주석. */
```

규칙:

- 항상 `/* ... */` (C 스타일)
- `//` **라인 주석 금지** (PintOS 는 ANSI C 호환을 유지하기 위함)
- `*` 와 텍스트 사이 공백 1 칸
- 마침표로 끝남 (한국어는 마침표 또는 종결어미)

### 6.2 다중 줄 주석 — 두 가지 스타일

#### 스타일 A (Stanford / list.c 등)

```c
/* 첫 줄.
   둘째 줄은 첫 텍스트 위치에 정렬.
   셋째 줄. */
```

규칙:

- 첫 줄 `/* `다음 텍스트.
- 둘째 줄부터 텍스트의 시작 컬럼을 첫 줄 텍스트와 정렬 (보통 3 칸 들여쓰기).
- 마지막 줄 끝에 `*/`.

#### 스타일 B (수정된 PintOS / GNU)

```c
/* 첫 줄.
 * 둘째 줄은 별표로 시작.
 * 셋째 줄. */
```

규칙:

- 둘째 줄부터 `*` (공백 + 별표 + 공백) prefix.
- 별표는 첫 줄 `/*` 의 `*` 와 정렬.

#### 어느 스타일을 쓰나

- **PintOS 원본 코드** = 스타일 A.
- **이 프로젝트의 한글 주석** = 스타일 B 도 자주 사용.
- **신규 작성 시** = 한 파일 안에서는 한 가지로 통일. 혼용 금지.

### 6.3 인라인 주석 — 컬럼 정렬

struct 멤버나 변수 선언 옆 인라인 주석은 **같은 컬럼으로 정렬**:

```c
struct thread {
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름 (디버깅용). */
	int64_t wakeup_tick;                /* sleep 깨어날 절대 tick. */
	int priority;                       /* 우선순위 (0~63). */

	struct list_elem elem;              /* 리스트 원소. */
};
```

규칙:

- 컬럼 정렬은 **TAB 이 아닌 SPACE** 사용 (TAB 폭이 IDE 마다 달라서).
- 정렬 컬럼은 가장 긴 선언 + 2 칸.
- 여러 선언이 한 묶음일 때 같은 컬럼.

### 6.4 함수 위 주석

함수 정의 바로 위:

```c
/* 현재 스레드를 wakeup_tick까지 잠재운다.
   sleep_list에 wakeup_tick 오름차순으로 정렬 삽입 후 block한다.
   인터럽트 핸들러에서 호출 금지. idle 스레드도 호출 금지. */
void
thread_sleep (int64_t wakeup_tick) {
	...
}
```

규칙:

- 함수의 **목적, 동작, 전제 조건** 을 명시.
- 주석은 함수 정의 바로 위 (빈 줄 없이).

### 6.5 파일 상단 주석

큰 모듈은 파일 상단에 모듈 설명:

```c
/* devices/timer.c -- 8254 PIT 기반 시스템 타이머.
   매 1/100 초마다 타이머 인터럽트를 발생시켜 ticks 를 증가시키고,
   thread_tick() 으로 스케줄러에 알린다.
   timer_sleep() 은 thread_sleep() 으로 위임한다. */

#include "devices/timer.h"
...
```

---

## 7. 중괄호 위치 — K&R 변형

### if / for / while / do-while

opening brace 는 **같은 줄**:

```c
if (cond) {
	...
} else if (other) {
	...
} else {
	...
}

for (i = 0; i < n; i++) {
	...
}

while (cond) {
	...
}

do {
	...
} while (cond);
```

### 단일 문장 블록 — brace 생략 허용

PintOS 는 단일 문장이면 brace 생략을 자주 사용:

```c
if (curr != idle_thread)
	list_push_back (&ready_list, &curr->elem);

while (timer_elapsed (start) < ticks)
	thread_yield ();
```

규칙:

- 본문 1 줄이면 brace 생략 OK.
- 본문 2 줄 이상이면 brace 필수.
- if-else 의 한쪽이 brace 면 양쪽 다 brace 권장 (혼용 피하기).

### switch

```c
switch (state) {
case THREAD_RUNNING:
	...
	break;
case THREAD_READY:
	...
	break;
default:
	NOT_REACHED ();
}
```

규칙:

- `case` 는 `switch` 와 **같은 인덴트** (들여쓰지 않음 — GNU 스타일).
- `case` 본문은 1 TAB 인덴트.

### struct / union / enum

opening brace **같은 줄**:

```c
struct thread {
	tid_t tid;
	...
};

enum thread_status {
	THREAD_RUNNING,
	THREAD_READY,
	THREAD_BLOCKED,
	THREAD_DYING
};
```

세미콜론 `;` 잊지 말 것.

---

## 8. 공백 규칙

### 이항 연산자 양쪽 공백 1 칸

```c
a + b           /* good */
a == b
i < n
ptr->field      /* 화살표는 공백 없음 */
obj.field       /* 점도 공백 없음 */
```

### 단항 연산자 — 공백 없음

```c
!cond
*ptr
&var
++i
--count
sizeof (struct thread)    /* sizeof 도 함수처럼 공백 + ( */
```

### 콤마 뒤 공백

```c
func (a, b, c);
int x, y, z;
```

### 키워드 뒤 공백

```c
if (cond) ...
while (cond) ...
for (i = 0; i < n; i++) ...
return value;
return;          /* return 단독은 공백 없이 ; */
sizeof (type)
```

### 캐스트

```c
(int) value
(struct thread *) ptr
```

캐스트와 피연산자 사이 공백 1 칸.

### 세미콜론 앞 공백 없음

```c
i++;             /* good */
i++ ;            /* bad */
```

### 줄 끝 공백 / TAB 금지

저장 시 trailing whitespace 자동 제거 IDE 설정 권장.

---

## 9. 헤더 파일 (.h)

### 헤더 가드

```c
#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

/* ... 내용 ... */

#endif /* threads/thread.h */
```

규칙:

- 매크로 이름: `<DIR>_<FILE>_H` 대문자.
- 끝의 `#endif` 옆에 **주석으로 헤더 경로** 명시 (`/* threads/thread.h */`).
- `#pragma once` 사용 금지 (PintOS 표준 아님).

### include 순서

표준 순서 (위에서 아래로):

```c
#include "threads/thread.h"        /* 1. 자기 헤더 (.c 파일이라면) */

#include <debug.h>                 /* 2. 시스템 / lib (꺽쇠) */
#include <stddef.h>
#include <stdio.h>

#include "threads/interrupt.h"     /* 3. 프로젝트 헤더 (큰따옴표) */
#include "threads/synch.h"
#include "devices/timer.h"

#ifdef USERPROG
#include "userprog/process.h"      /* 4. 조건부 include 는 마지막 */
#endif
```

각 그룹 사이 빈 줄 1.

### .h 에 정의 vs 선언

- **선언 (declarations)** 만 헤더에: 함수 prototype, struct 정의, typedef, 매크로.
- **정의 (definitions)** 는 .c 파일에: 함수 본문, 전역 변수.
- 예외: `static inline` 함수는 헤더에 정의 가능.

### 헤더에 #include 최소화

다른 .c 가 이 헤더를 include 할 때 끌려오는 의존성을 최소화. **Forward declaration** 활용:

```c
/* threads/thread.h */
struct lock;                      /* forward — 전체 정의 불필요 */

void some_func (struct lock *);   /* 포인터만 쓰면 forward 로 충분 */
```

---

## 10. 네이밍 컨벤션

### 함수 / 변수

**snake_case** (소문자 + underscore):

```c
void thread_create (...);
void timer_sleep (...);
int64_t wakeup_tick;
struct thread *current_thread;
```

피해야 할 이름:

안 좋음권장`t`, `e`, `nthread`, `elem`, `countfunc1`, `do_stuffcompute_priority`, `update_load_avgtmpold_priority`, `prev_stateflagis_blocked`, `should_yield`

예외 — 관용적 짧은 이름:

- 루프 인덱스: `i`, `j`, `k`
- 람다·짧은 콜백 인자: `e` (list_elem 순회 등) — 단, 의미 모호하면 풀어쓰기

### 비교 함수의 인자명

`a`, `b` 보다 명확한 이름:

```c
static bool
wakeup_tick_less (const struct list_elem *lhs,
                  const struct list_elem *rhs,
                  void *aux UNUSED) {
	const struct thread *lhs_thread = list_entry (lhs, struct thread, elem);
	const struct thread *rhs_thread = list_entry (rhs, struct thread, elem);
	return lhs_thread->wakeup_tick < rhs_thread->wakeup_tick;
}
```

추천: `lhs` / `rhs` (left/right hand side) 또는 `left` / `right`.

### 매크로 / 상수 / enum 값

**UPPER_CASE_SNAKE**:

```c
#define PRI_MIN 0
#define PRI_DEFAULT 31
#define PRI_MAX 63

enum thread_status {
	THREAD_RUNNING,
	THREAD_READY,
	THREAD_BLOCKED,
	THREAD_DYING
};
```

### typedef

소문자 + `_t` 접미사:

```c
typedef int tid_t;
typedef int64_t off_t;
```

PintOS 에서는 typedef 를 자주 쓰지 않음 — struct 는 보통 `struct thread` 그대로 사용. 별칭 만드는 건 정수 타입이나 함수 포인터 정도.

### struct 이름

소문자 + snake_case:

```c
struct thread { ... };
struct list_elem { ... };
struct sleep_test { ... };          /* 신규 정의 시도 동일 */
```

### static 함수도 동일

`static` 함수도 일반 함수와 같은 네이밍 (snake_case):

```c
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool wakeup_tick_less (...);
```

---

## 11. 파일 구조 — .c 파일 함수 위치 순서

권장 순서 (위에서 아래로):

```
1. 파일 상단 주석 (모듈 설명, 저작권)
2. include 들 (자기 헤더 → system → 프로젝트)
3. 매크로 / 상수 / typedef
4. 전역 변수
   - static 변수 먼저 (모듈 내부)
   - public 변수 다음 (extern)
5. static 함수 prototype
6. 공개 함수 정의 (헤더에 선언된 것)
7. static 함수 정의
```

### 함수 그룹화

같은 목적의 함수들을 묶고, 그룹 사이를 빈 줄 2 줄 + 그룹 헤더 주석으로 구분:

```c
/* ============================================================
 * 스레드 생명주기
 * ============================================================ */

void
thread_init (void) { ... }

void
thread_start (void) { ... }

tid_t
thread_create (...) { ... }

void
thread_exit (void) { ... }


/* ============================================================
 * 스케줄링
 * ============================================================ */

void
thread_block (void) { ... }

void
thread_unblock (struct thread *t) { ... }

void
thread_yield (void) { ... }
```

---

## 12. 라인 길이

- **80 컬럼 권장**.
- 100 컬럼까지는 허용 (긴 함수 호출, 긴 문자열 등).
- 그 이상은 줄바꿈.

### 긴 줄 줄바꿈

#### 함수 호출

```c
list_insert_ordered (&sleep_list, &curr->elem,
                     wakeup_tick_less, NULL);
```

#### 긴 조건

```c
if (sleeper->wakeup_tick <= current_tick
    && !list_empty (&sleep_list)) {
	...
}
```

연산자를 **다음 줄 시작에** (가독성).

#### 긴 문자열

```c
PANIC ("kernel: failed to acquire lock %p "
       "by thread %s", lock, thread_name ());
```

문자열은 자동으로 인접 결합 (C 표준).

---

## 13. 매직 넘버 / 상수

숫자 리터럴은 의미 있는 매크로로:

```c
/* good */
#define PRI_MIN 0
#define PRI_DEFAULT 31
#define PRI_MAX 63

if (priority > PRI_MAX) ...

/* bad */
if (priority > 63) ...
```

예외:

- 0, 1 같은 자명한 값
- 비트 연산: `1 << 3`
- 인덱스 0, -1 (NULL/error 의미)

---

## 14. ASSERT 사용

함수 진입점에서 **invariant 검증** :

```c
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);

	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}
```

### 언제 쓰나

- **프리컨디션** : 호출자가 보장해야 할 조건 (인자 NULL 아님, 락 보유, 인터럽트 OFF 등).
- **포스트컨디션** : 함수 종료 시 보장되는 상태.
- **invariant** : 데이터 구조의 일관성 (sleep_list 정렬됨 등).

### 언제 쓰지 않나

- **사용자 입력 검증** (정상 흐름의 일부 — `if` 로 처리하고 에러 반환)
- **자주 실패하는 조건** (`malloc` 실패 등 — 정상 처리)

ASSERT 실패 = 커널 PANIC. 즉 "이 조건이 깨지면 코드 자체에 버그" 인 경우만.

---

## 15. 인터럽트 안전 코드

### 인터럽트 핸들러에서 금지

```
✗ thread_block ()           /* 잠들면 안 됨 */
✗ malloc / free             /* lock 획득 시도 */
✗ lock_acquire              /* lock 시도 */
✗ sema_down                 /* 블록 시도 */
✗ printf 등 큰 작업          /* 너무 오래 걸림 */
```

### 인터럽트 핸들러에서 허용

```
✓ thread_unblock           /* 깨우기는 OK */
✓ sema_up                  /* 시그널은 OK */
✓ list 조작                /* 빠른 조작 */
✓ 짧은 메모리 접근
```

### Critical section 보호

전역 자료구조 조작은 인터럽트 비활성화로 보호:

```c
enum intr_level old_level = intr_disable ();

/* critical section */
list_insert_ordered (&sleep_list, &curr->elem, wakeup_tick_less, NULL);
curr->wakeup_tick = wakeup_tick;
thread_block ();

intr_set_level (old_level);
```

규칙:

- `old_level` 같은 변수에 이전 상태 저장.
- 종료 시 원래 상태로 복구 (`intr_set_level`).
- 단순 `intr_enable()` 으로 강제 활성화 금지 (호출자가 OFF 였을 수 있음).

---

## 16. 한국어 주석 정책 — 본 프로젝트 한정

이 프로젝트는 PintOS 코드를 **한국어로 학습** 하기 위해 주석을 한글로 번역했다. 신규 코드 작성 시:

### 권장

- 함수 위 주석, 파일 상단 주석은 **한국어** 로.
- struct 멤버 인라인 주석도 한국어.
- 변수 의미 설명도 한국어.

### 절대 한국어로 바꾸지 말 것

- 채점 스크립트 (`*.ck`) 가 매칭하는 **출력 문자열** :
  - `msg ("Creating %d threads to sleep ...")`
  - `fail ("thread %d woke up out of order...")`
  - `pass ()` 의 PASS/FAIL 메시지
- **함수명 / 변수명 / 식별자** 전부 영문 유지.
- **PANIC / ASSERT** 의 메시지도 영문 유지 (디버깅 호환성).

---

## 17. 종합 예시 — Good vs Bad

### Good

```c
/* devices/timer.c */

/* 대략 TICKS 만큼의 타이머 틱 동안 실행을 중단한다.
   ticks <= 0 이면 즉시 반환한다. */
void
timer_sleep (int64_t ticks) {
	if (ticks <= 0) return;

	ASSERT (intr_get_level () == INTR_ON);
	thread_sleep (timer_ticks () + ticks);
}

/* 타이머 인터럽트 핸들러. 매 1/TIMER_FREQ 초 호출. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
	thread_awake (ticks);
}
```

체크리스트:

- ✓ TAB 인덴트
- ✓ 함수 정의: 반환타입 별도 줄
- ✓ `func ()` 공백
- ✓ opening brace 같은 줄
- ✓ 한국어 주석, `/* ... */` 형식
- ✓ ASSERT 로 프리컨디션 검증
- ✓ static 함수도 같은 스타일

### Bad

```c
//timer.c — comment style wrong, no description

void timer_sleep(int64_t ticks)    // ( 앞 공백 없음, 인자명 약어
{                                   // brace 다음 줄 (PintOS 아님)
    if(ticks<=0)return;             // 공백 없음, brace 없음, 한 줄에 두 문장
    
    ASSERT(intr_get_level()==INTR_ON);  // 공백 없음
    thread_sleep(timer_ticks()+ticks);
}

static void timer_interrupt(struct intr_frame *args UNUSED){ticks++;thread_tick();thread_awake(ticks);}  // 한 줄 압축
```

체크리스트 (전부 위반):

- ✗ `//` 주석
- ✗ 함수 정의 brace 다음 줄
- ✗ `func(` 공백 없음
- ✗ 4-space 인덴트 (TAB 아님)
- ✗ 연산자 양쪽 공백 없음
- ✗ if-return 한 줄
- ✗ static 함수 한 줄 압축

---

## 18. 빠른 체크리스트 (PR 리뷰용)

신규/수정 .c, .h 파일을 커밋 전 이 체크리스트로 검증:

### 포맷팅

- \[ \] 인덴트가 TAB 인가
- \[ \] 함수 정의가 `반환타입\n함수명 (인자) {` 형식인가
- \[ \] 함수 호출에 `func (` 공백이 있는가
- \[ \] 이항 연산자 양쪽 공백 있는가
- \[ \] 콤마 뒤 공백 있는가
- \[ \] 줄 끝 trailing whitespace 없는가
- \[ \] 80 컬럼 이내인가 (불가피하면 100)

### 주석

- \[ \] `/* ... */` 만 사용 (`//` 없음)
- \[ \] 다중 줄 주석 정렬 일관됨 (스타일 A 또는 B)
- \[ \] 인라인 주석이 같은 컬럼으로 정렬됨
- \[ \] 함수 위 주석이 목적·전제조건 설명함

### 헤더

- [ ] 헤더 가드 `<DIR>_<FILE>_H`
- [ ] `#endif` 뒤에 헤더 경로 주석
- [ ] include 순서: 자기헤더 → system → 프로젝트
- [ ] 헤더에 정의 (function body, 전역 변수) 없는가 (선언만)

### 네이밍

- [ ] 함수/변수: snake_case
- [ ] 매크로/상수: UPPER_SNAKE
- [ ] struct: snake_case
- [ ] 1 글자 변수 없는가 (루프 인덱스 제외)
- [ ] 비교 함수 인자가 `a`/`b` 가 아닌 의미 있는 이름인가

### 안전성

- [ ] 인터럽트 핸들러 안에서 thread_block / malloc / lock_acquire 호출 없는가
- [ ] critical section 이 intr_disable / intr_set_level 로 보호됐는가
- [ ] ASSERT 가 적절한 위치에 있는가

### 한국어 주석 (본 프로젝트)

- [ ] 함수 위 주석, struct 멤버 주석이 한국어인가
- [ ] msg/fail/printf 의 문자열 인자는 영문 그대로인가
- [ ] 함수명·변수명은 영문인가

---

## 19. 자동화 가능성 — clang-format 설정 예시

PintOS 스타일을 강제하려면 `.clang-format` 사용 가능. 단, GNU 스타일에 가깝게 조정:

```yaml
# .clang-format (PintOS 용 시안)
BasedOnStyle: GNU
IndentWidth: 8
UseTab: Always
TabWidth: 8
BreakBeforeBraces: Linux
SpaceBeforeParens: Always
ContinuationIndentWidth: 8
ColumnLimit: 80
PointerAlignment: Right
AlignTrailingComments: true
```

⚠️ 자동 포매터는 PintOS 의 모든 관행을 완벽 재현 못함 (특히 인라인 주석 정렬). **자동화 결과를 절대 신뢰하지 말고**, 수동 검토 필수.

---

## 20. 참고 자료

- 기존 PintOS 소스 (`pintos/threads/thread.c`, `pintos/devices/timer.c`, `pintos/lib/kernel/list.c`) — 살아있는 레퍼런스.
- GNU C Coding Standards: https://www.gnu.org/prep/standards/standards.html
- 본 저장소의 일반 C 컨벤션: `docs/convention/c-style.md` (★ PintOS 와 다름. 혼동 주의.)
- PintOS 학습 가이드: `docs/pintos/01-big-picture.md`, `docs/pintos/11-debug-setup.md`.

---

## 변경 이력

| 날짜 | 변경 |
| --- | --- |
| 2026-04-25 | 초판. PintOS 소스 (thread.c, timer.c, synch.h, list.c 등) 분석 기반. |
