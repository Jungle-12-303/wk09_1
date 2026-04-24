# Phase 1: Alarm Clock 테스트 분석

## 목표

현재 `timer_sleep()`은 **busy waiting**(while 루프로 시간이 될 때까지 CPU를 낭비)으로 구현되어 있다.
이것을 **sleep/wakeup 방식**으로 바꿔야 한다. 즉, 스레드를 블록시켰다가 정해진 tick에 깨운다.

## 수정 대상 파일

### `devices/timer.c` — 핵심 수정

**현재 코드 (busy wait):**
```c
void timer_sleep(int64_t ticks) {
    int64_t start = timer_ticks();
    ASSERT(intr_get_level() == INTR_ON);
    while (timer_elapsed(start) < ticks)
        thread_yield();  // ← CPU를 낭비하며 대기
}
```

**변경할 내용:**
1. `timer_sleep()`: 현재 스레드의 wakeup_tick을 설정하고 `thread_block()` 호출
2. `timer_interrupt()` (매 tick마다 호출됨): sleep 리스트를 확인하여 깨울 시간이 된 스레드를 `thread_unblock()`

### `threads/thread.h` — 구조체 수정

`struct thread`에 필드 추가:
```c
int64_t wakeup_tick;  // 깨어날 tick 시각
```

### `threads/thread.c` — sleep 리스트 관리

전역 sleep 리스트 추가 및 관련 함수 구현.

---

## 테스트 6개 상세

### 1. alarm-single (배점: 1)

**소스:** `alarm-wait.c` → `test_sleep(5, 1)`

**동작:** 5개 스레드를 생성하여 각각 1회씩 sleep.
- thread 0: 10 tick sleep
- thread 1: 20 tick sleep
- thread 2: 30 tick sleep
- thread 3: 40 tick sleep
- thread 4: 50 tick sleep

**통과 조건:** 깨어나는 순서가 `product = iteration × duration` 기준으로 **비내림차순(nondescending)** 이어야 함.

**기대 출력 (alarm.pm이 검증):**
```
thread 0: duration=10, iteration=1, product=10
thread 1: duration=20, iteration=1, product=20
thread 2: duration=30, iteration=1, product=30
thread 3: duration=40, iteration=1, product=40
thread 4: duration=50, iteration=1, product=50
```

**핵심 포인트:** sleep한 시간이 짧은 순서대로 깨어나야 한다. busy wait 제거의 기본 검증.

---

### 2. alarm-multiple (배점: 1)

**소스:** `alarm-wait.c` → `test_sleep(5, 7)`

**동작:** 5개 스레드가 각각 **7회 반복** sleep.
- 총 35번의 wakeup이 product 기준 비내림차순으로 발생해야 함

**통과 조건:** alarm-single과 동일한 정렬 조건, 반복 7회.

**핵심 포인트:** 여러 스레드가 반복적으로 sleep/wakeup하는 상황을 테스트. sleep 리스트 관리가 정확해야 한다.

---

### 3. alarm-simultaneous (배점: 1)

**소스:** `alarm-simultaneous.c` → `test_sleep(3, 5)`

**동작:** 3개 스레드가 모두 **같은 시점**(10 tick 간격)에 sleep → 동시에 깨어남을 5회 반복.

**기대 출력:**
```
iteration 0, thread 0: woke up after 10 ticks
iteration 0, thread 1: woke up 0 ticks later
iteration 0, thread 2: woke up 0 ticks later
iteration 1, thread 0: woke up 10 ticks later
iteration 1, thread 1: woke up 0 ticks later
iteration 1, thread 2: woke up 0 ticks later
... (5회 반복)
```

**통과 조건:** 같은 tick에 깨어나야 할 스레드들이 정확히 같은 tick에 깨어나야 함 (`0 ticks later`).

**핵심 포인트:** wakeup_tick이 같은 스레드들을 정확한 tick에 모두 깨워야 한다. 하나라도 1 tick 늦으면 실패.

---

### 4. alarm-priority (배점: 2)

**소스:** `alarm-priority.c`

**동작:**
1. 우선순위 21~30인 스레드 10개 생성
2. 모두 같은 시점(`wake_time`)까지 sleep
3. 동시에 깨어날 때 **우선순위 높은 순서(30→21)** 로 실행되어야 함

**기대 출력:**
```
Thread priority 30 woke up.
Thread priority 29 woke up.
Thread priority 28 woke up.
...
Thread priority 21 woke up.
```

**통과 조건:** wakeup 후 우선순위 순서대로 실행.

**핵심 포인트:** 이 테스트는 Phase 1과 Phase 2의 교차점. sleep에서 깨어난 스레드들이 ready_list에 들어갈 때 우선순위 순으로 정렬되어야 한다. Phase 2(Priority Scheduling)를 먼저 구현하거나, 최소한 `thread_unblock()` 시 ready_list를 우선순위 순으로 관리해야 통과.

---

### 5. alarm-zero (배점: 1)

**소스:** `alarm-zero.c`

**동작:** `timer_sleep(0)` 호출.

**기대 출력:**
```
PASS
```

**통과 조건:** 크래시 없이 즉시 리턴.

**핵심 포인트:** `ticks <= 0`이면 즉시 리턴하는 경계 조건 처리.

---

### 6. alarm-negative (배점: 1)

**소스:** `alarm-negative.c`

**동작:** `timer_sleep(-100)` 호출.

**기대 출력:**
```
PASS
```

**통과 조건:** 크래시 없이 즉시 리턴.

**핵심 포인트:** 음수 tick에 대한 방어 코드.

---

## 구현 가이드

### 1단계: thread 구조체에 wakeup_tick 추가

```c
// threads/thread.h  — struct thread 안에 추가
int64_t wakeup_tick;          // 깨어날 tick
struct list_elem sleep_elem;  // sleep 리스트용 elem (선택)
```

### 2단계: sleep 리스트 생성

```c
// threads/thread.c 또는 devices/timer.c
static struct list sleep_list;  // 잠든 스레드 리스트
```

### 3단계: timer_sleep() 수정

```c
void timer_sleep(int64_t ticks) {
    if (ticks <= 0) return;  // alarm-zero, alarm-negative 처리

    int64_t start = timer_ticks();
    ASSERT(intr_get_level() == INTR_ON);

    struct thread *cur = thread_current();
    cur->wakeup_tick = start + ticks;

    enum intr_level old_level = intr_disable();
    list_insert_ordered(&sleep_list, &cur->elem, cmp_wakeup_tick, NULL);
    thread_block();
    intr_set_level(old_level);
}
```

### 4단계: timer_interrupt()에서 wakeup 처리

```c
static void timer_interrupt(struct intr_frame *args UNUSED) {
    ticks++;
    thread_tick();

    // sleep_list에서 깨울 스레드 확인
    while (!list_empty(&sleep_list)) {
        struct thread *t = list_entry(list_front(&sleep_list),
                                       struct thread, elem);
        if (t->wakeup_tick > ticks) break;
        list_pop_front(&sleep_list);
        thread_unblock(t);
    }
}
```

### 주의사항

- `timer_sleep()`에서 `thread_block()` 호출 전에 반드시 **인터럽트를 비활성화**해야 한다. 그렇지 않으면 블록하기 전에 타이머 인터럽트가 끼어들 수 있다.
- sleep_list를 wakeup_tick 기준으로 **정렬 삽입**하면, `timer_interrupt()`에서 리스트 앞쪽만 확인하면 되므로 O(1)으로 처리 가능.
- alarm-priority 테스트를 통과하려면 `thread_unblock()`에서 ready_list에 우선순위 순으로 삽입해야 한다 (Phase 2 내용).
