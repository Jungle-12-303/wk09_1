---
type: Lab
status: Draft
week:
  - threads
systems:
  - Linux
  - Windows
  - PintOS
  - QEMU
tags:
  - domain:os
  - domain:pintos
  - domain:qemu
  - week:threads
  - layer:kernel
  - topic:scheduler
  - topic:casting
  - topic:byte-buffer
  - topic:gdb
related_to:
  - "[[concept-to-code-map]]"
  - "[[week-1-threads-map]]"
  - "[[context-switch-trace]]"
  - "[[바이트-버퍼와-캐스팅-실험|바이트 버퍼와 캐스팅 실험]]"
---

# PintOS intrusive list 실험 (list_entry가 struct thread를 되찾는 법)

## 작은 질문

`ready_list`에서 꺼낸 건 `struct list_elem *`인데, 어떻게 `struct thread *`로 “되돌아갈” 수 있을까?

그리고 왜 PintOS는 리스트 노드를 따로 `malloc()`해서 만들지 않고, `struct thread` 안에 `struct list_elem elem`을 “끼워 넣는” 방식(= intrusive list)을 쓸까?

## 왜 필요한가

PintOS Threads 과제에서 거의 모든 자료구조는 아래 패턴으로 연결된다.

- 스케줄러 run queue: `ready_list`에 `thread->elem`
- sleep queue: `sleep_list`에 `thread->elem`
- 세마포어 대기열: `semaphore->waiters`에 `thread->elem`

즉, 리스트를 이해하지 못하면 “스레드가 ready/blocked/sleeping으로 이동한다”는 말을 코드에서 읽을 수 없다.

## 핵심 모델

intrusive list의 핵심은 이것 하나다.

- **리스트 노드가 객체 바깥에 있지 않고, 객체(struct)의 필드로 포함되어 있다.**
- 그래서 리스트는 `struct list_elem *`만 들고 다니지만,
- 우리는 `list_entry()`로 “이 list_elem을 포함한 진짜 객체 포인터”를 다시 계산한다.

## 예시 상황 (주소 한 번만 계산해보기)

아래는 *가상의 숫자*로 계산 감각을 잡는 예시다.

- 어떤 스레드 객체가 `t = (struct thread *) 0x80000000`에 있다고 하자.
- 그 안의 `t->elem`이 구조체 오프셋 `0x120`에 위치한다고 하자.

그러면:

```text
&t->elem          = 0x80000120
&t->elem.next     = 0x80000128   (포인터 8바이트라면 next는 elem+8)

list_entry(e, struct thread, elem)
  = (struct thread *)((uint8_t *)&e->next - offsetof(struct thread, elem.next))
  = (struct thread *)(0x80000128 - 0x128)
  = (struct thread *)0x80000000
```

이 말은 즉:

- `list_entry`는 “포인터 산술(pointer arithmetic)”로 **바깥 구조체의 시작 주소를 역산**한다.

## Linux / Windows에서는

현실 OS에서도 intrusive list는 흔하다.

- Linux 커널은 `struct list_head`를 객체에 포함시키고 `container_of()`로 되돌아간다.
- Windows는 커널 내부에 다양한 리스트/큐 구현이 있고, 개념적으로는 “노드가 오브젝트에 포함”되는 방식이 자주 쓰인다.

현실 OS는 동시성, 메모리 배치, 캐시 locality 같은 이유로 이런 패턴을 광범위하게 사용한다.

## PintOS에서는 (코드 증거)

### 1) list_entry는 어디에 있나?

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/lib/kernel/list.h`
  - `struct list_elem { struct list_elem *prev, *next; }`
  - `#define list_entry(LIST_ELEM, STRUCT, MEMBER) ... offsetof (STRUCT, MEMBER.next)`

이 매크로를 보면 intrusive list가 “주소 계산”으로 성립한다는 걸 알 수 있다.

### 2) thread는 왜 list_elem을 들고 있나?

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/thread.h`
  - `struct thread` 안에 `struct list_elem elem;`이 있다.
  - 주석에 `elem`이 **ready_list 원소**이기도 하고 **semaphore waiters 원소**이기도 하다고 적혀 있다.
  - 그리고 “둘은 배타적”이기 때문에 하나의 `elem`을 재사용할 수 있다고 설명한다.

즉, 스레드가 동시에 ready 상태이면서 blocked 상태일 수 없기 때문에,

- ready일 때: `ready_list`에 들어가고
- blocked일 때: `semaphore->waiters`에 들어간다

(둘 중 하나만)

### 3) ready_list에서 thread를 꺼내는 순간

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/thread.c`
  - `next_thread_to_run()`에서 `list_entry(list_pop_front(&ready_list), struct thread, elem)`로 다음 스레드를 얻는다.

여기서 `list_pop_front()`가 주는 건 `struct list_elem *`인데, `list_entry()`가 `struct thread *`로 되돌린다.

## QEMU에서는 (비슷하지만 역할은 다름)

QEMU는 스레드 스케줄링을 하지 않지만, QEMU 코드도 intrusive list/queue 매크로를 많이 쓴다.

- `/Users/woonyong/workspace/Krafton-Jungle/QEMU/include/qemu/queue.h`
  - `QLIST_HEAD`, `QLIST_ENTRY`, `QLIST_FOREACH` 같은 매크로 기반 자료구조

여기서 배울 포인트는 “OS가 아니라도 시스템 코드는 intrusive list를 자주 쓴다”는 사실이다.

## 차이점

| 항목 | Linux / Windows | PintOS | QEMU |
|---|---|---|---|
| 목적 | 성능/동시성/구조화 | 학습용으로 단순화 | 에뮬레이터 내부 자료구조 |
| 노드 위치 | 오브젝트 내부가 흔함 | `struct thread` 내부에 `elem` | 다양한 queue 매크로 |
| 되돌리기 | `container_of()`류 | `list_entry()` | 매크로별로 다름 |

## 숫자와 메모리 (GDB로 오프셋을 직접 보기)

목표: “`thread*`와 `&thread->elem`의 주소 차이 = 오프셋”을 눈으로 확인한다.

1) `thread_yield` 또는 `next_thread_to_run`에 breakpoint

2) 현재 스레드 주소와 elem 주소 확인

```gdb
p/x t
p/x &t->elem
p/x (uint8_t*)&t->elem - (uint8_t*)t
```

3) ready_list에서 꺼낸 elem로부터 base를 되찾는지 확인

```gdb
p/x e
p/x &e->next
# (개념적으로) base = &e->next - offsetof(struct thread, elem.next)
```

`offsetof` 값은 C 컴파일 타임 상수라서, 실제 오프셋은 위 2)에서 “주소 차이”로 확인하는 편이 더 간단하다.

## 직접 확인 (실험 체크리스트)

- `next_thread_to_run()`에서 `list_entry()` 직후 `t->name`이 정상인지
- `thread_unblock()`에서 `&t->elem`이 `ready_list`에 들어갔는지
- `sema_down()` 경로에서 blocked thread가 `semaphore->waiters`에 들어갈 때도 같은 `elem`이 쓰이는지

## 다음으로 볼 문서

- [[context-switch-trace]]: “스레드가 바뀐다”를 레지스터/스택 관점으로 추적
- [[interrupt-timer-qemu]]: tick이 왜 선점(preemption)으로 이어지는지
