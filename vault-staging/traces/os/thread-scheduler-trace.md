---
type: Trace
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
  - topic:interrupt
related_to:
  - "[[concept-to-code-map]]"
  - "[[week-1-threads-map]]"
  - "[[interrupt-timer-qemu]]"
  - "[[context-switch-trace]]"
  - "[[pintos-intrusive-list-lab]]"
---

# 스레드 스케줄러 (Ready List) Trace

## 작은 질문

PintOS에서 “다음에 실행할 스레드는 누가 결정하나?” 그리고 “ready_list에는 누가/언제 들어가나?”

이 문서는 **컨텍스트 스위치(레지스터/스택 교체)** 자체가 아니라, 그 직전에 있는 **스케줄러의 선택 로직(ready_list 정책)** 을 눈으로 추적하는 것이 목표다.

## 왜 필요한가

스레드 과제에서 가장 흔한 버그는 다음 둘 중 하나다.

- “ready_list에 들어가야 할 스레드가 안 들어감” → 영원히 실행되지 않음
- “ready_list에서 꺼내면 안 되는 값이 꺼내짐” → `ASSERT (is_thread (next))` 같은 커널 패닉

즉, 스케줄러를 이해한다는 건 “상태 전이(READY/RUNNING/BLOCKED/DYING)가 언제 일어나는지”와 “ready_list가 어떤 규칙으로 관리되는지”를 이해하는 것이다.

## 핵심 모델 (머릿속에 넣을 최소 모델)

PintOS(Threads)의 스케줄러 모델을 한 문장으로 요약하면:

- **ready_list = 지금 당장 실행 가능한 스레드들의 줄(run queue)**  
- **schedule() = 그 줄에서 다음 주인을 뽑고(thread 선택), 실제 전환은 context switch 코드로 내려감**

그리고 “스레드가 CPU를 놓는 순간”은 크게 4가지다.

1) **자발적 양보**: `thread_yield()`  
2) **블록**: `thread_block()` (락/세마포어/조건변수 등)  
3) **종료**: `thread_exit()`  
4) **선점**: timer interrupt → `intr_yield_on_return()` → “인터럽트가 끝날 때” `thread_yield()`

## 예시 상황 (간단한 그림으로)

스레드가 3개 있다고 하자.

- A: priority 10
- B: priority 50
- C: priority 30

ready_list는 “다음 실행 후보”의 모음이고, PintOS 코드는 ready_list를 **priority 내림차순으로 유지**하도록 구현돼 있다(아래 PintOS 코드 섹션 참고).

즉 ready_list의 맨 앞은 언제나 “지금 가장 높은 priority의 스레드”가 된다.

## Linux / Windows에서는 (현실 기준으로 잡기)

현실 OS는 PintOS보다 훨씬 복잡한 선택을 한다.

- SMP에서 CPU마다 run queue가 있고, load balancing이 있다.
- priority뿐 아니라 fairness, latency, I/O bound/CPU bound 특성, tickless, per-CPU timer 등 수많은 요소가 섞인다.

하지만 “run queue에 넣고(dequeue/enqueue), 다음을 고르고(pick), 전환한다(switch)”라는 큰 틀은 동일하다.

PintOS는 이 큰 틀을 **ready_list 하나로 매우 단순화**해서 학습하게 만든다.

## PintOS에서는 (코드로 내려가기)

이 Trace의 핵심 파일:

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/thread.c`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/threads/thread.h`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/include/lib/kernel/list.h`

### 1) ready_list는 어디에 있고, 무엇을 담나?

- `thread.c`에 `static struct list ready_list;`
- ready_list의 원소는 “`struct thread` 자체”가 아니라, `struct thread` 안에 들어있는 `struct list_elem elem`이다.
  - 이게 intrusive list(내장 리스트) 패턴이다. 자세한 원리는 [[pintos-intrusive-list-lab]]에서 실험으로 확인한다.

### 2) “ready 상태로 들어가는 순간”은 어디인가?

대표 경로는 다음 2개다.

1) **blocked → ready**: `thread_unblock(t)`
   - `t->elem`을 ready_list에 삽입하고 `t->status = THREAD_READY`로 만든다.
2) **running → ready**: `thread_yield()`
   - 현재 스레드(단 idle 제외)를 ready_list에 다시 넣고, 자기 상태를 `THREAD_READY`로 바꾼 뒤 `schedule()`로 내려간다.

추가로 alarm-clock을 구현했다면:

- `timer_interrupt()` → `thread_awake()` → `thread_unblock(t)`  
  즉 “sleep_list에서 깨워져 ready_list로 이동”하는 경로가 생긴다.

### 3) ready_list는 어떤 정책으로 정렬되는가?

이 코드베이스에서는 ready_list 삽입이 다음처럼 구현돼 있다.

- `thread_unblock()`에서 `list_insert_ordered(&ready_list, &t->elem, thread_priority, NULL)`
- `thread_yield()`에서도 `list_insert_ordered(&ready_list, &curr->elem, thread_priority, NULL)`

그리고 비교 함수는 다음 의미를 가진다.

- `thread_priority(a, b) = a의 priority가 b보다 크면 true`
- 따라서 ready_list는 “priority 내림차순”으로 유지된다.

결론:

- `next_thread_to_run()`이 ready_list에서 **맨 앞(front)** 을 꺼내는 순간,
  그 값은 “현재 가장 높은 priority의 스레드”가 된다.

### 4) 다음 스레드는 어디서 뽑나?

- `next_thread_to_run()`
  - ready_list가 비어 있으면 `idle_thread`
  - 아니면 `list_pop_front(&ready_list)`로 하나를 꺼내고
  - `list_entry(..., struct thread, elem)`로 `struct thread*`로 되돌린다

이 `list_entry()`가 바로 “list_elem* → 바깥 구조체*”로 복원하는 매크로다.
이 원리를 정확히 이해하면 ready_list/sleep_list/세마포어 waiters가 한꺼번에 선명해진다.

### 5) schedule()는 “정책”을 담당하나, “전환”을 담당하나?

PintOS에서 `schedule()`은 “다음 후보 선택 + 전환 트리거”를 한다.

- 선택: `next = next_thread_to_run()`
- 상태 갱신: `next->status = THREAD_RUNNING`, `thread_ticks = 0`
- (USERPROG일 때) 주소 공간 갱신: `process_activate(next)`
- 전환: `thread_launch(next)`로 내려감

즉, 스케줄러의 “정책 자료구조”는 ready_list이고, 실제 “레지스터 교체”는 [[context-switch-trace]]의 `thread_launch()/do_iret()` 쪽이다.

### 6) 선점(preemption)은 어디서 시작되나?

PintOS에서 선점은 “타이머 인터럽트”에서 출발한다.

```text
timer_interrupt()  (devices/timer.c)
  -> thread_tick() (threads/thread.c)
     -> if thread_ticks >= TIME_SLICE:
          intr_yield_on_return() (threads/interrupt.c)
```

포인트:

- `thread_tick()`은 “지금 당장 schedule() 호출”이 아니라,
  “인터럽트가 끝날 때 양보(yield)해야 한다”를 예약한다.
- 실제 `thread_yield()`는 외부 인터럽트 처리가 끝나는 지점에서 실행된다.

이건 단순한 취향이 아니라 **인터럽트 문맥(intr_context)에서 할 수 없는 작업**을 피하기 위한 설계다.
(다음 실행에서 [[questions/os/]]에 짧게 분리해도 좋은 주제다.)

## QEMU에서는 (역할 분리)

QEMU는 “ready_list”도 “priority 정책”도 모른다.

- ready_list와 `schedule()`은 guest OS(PintOS) 내부의 자료구조/정책이다.
- QEMU는 “타이머 인터럽트가 들어오는 것처럼 보이게” 만들고, CPU 명령어의 효과(예: `iretq`)를 에뮬레이션한다.

즉, **스케줄러 정책은 PintOS**, **인터럽트/CPU 동작의 하드웨어 효과는 QEMU**가 담당한다.

## 숫자와 메모리 (TIME_SLICE=4가 의미하는 것)

PintOS 코드에서:

- `TIME_SLICE = 4`
- `thread_ticks`가 매 tick마다 1씩 증가한다.

그 의미는:

- 한 스레드가 (조건이 같다면) 최대 4 tick 동안 CPU를 연속으로 사용할 수 있다.
- 4번째 tick에서 `intr_yield_on_return()`이 예약되고,
  “인터럽트가 끝날 때” 스레드가 ready_list로 밀려나며 다음 스레드가 선택될 기회를 만든다.

## 직접 확인 (GDB 체크리스트)

다음 breakpoint/관찰 포인트를 잡으면 ready_list 정책이 눈에 보인다.

1) ready_list에 들어가는 순간 보기
   - breakpoint: `thread_unblock`, `thread_yield`
   - 확인: `p thread_current()->name`, `p t->name`

2) next가 어디서 나오는지 보기
   - breakpoint: `next_thread_to_run`, `schedule`
   - 확인: `p next->name`, `p next->priority`

3) 선점이 예약되는지 보기
   - breakpoint: `thread_tick`, `intr_yield_on_return`
   - 확인: `p thread_ticks`, `p yield_on_return` (interrupt.c 내부 플래그)

## 다음으로 볼 문서

- [[context-switch-trace]]: “레지스터/스택은 실제로 어떻게 바뀌나?”
- [[pintos-intrusive-list-lab]]: “왜 list에는 thread*가 아니라 elem*가 들어가나?”
- [[interrupt-timer-qemu]]: “QEMU 타이머가 어떻게 PintOS에 tick을 주나?”

