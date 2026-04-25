# Phase 2: Priority Scheduling 테스트 분석

## 목표

현재 Pintos는 **라운드 로빈(round-robin)** 스케줄링을 사용한다.
이것을 **우선순위 기반 스케줄링**으로 바꿔야 한다.

핵심 규칙:
- 항상 **가장 높은 우선순위의 스레드**가 CPU를 차지해야 한다.
- 우선순위가 같으면 **FIFO(먼저 ready_list에 들어온 순서)** 로 실행한다.
- 우선순위가 변경되면 즉시 스케줄링을 재평가해야 한다.

## 수정 대상 파일

### `threads/thread.c` — 핵심 수정

1. **`thread_unblock()`**: ready_list에 넣을 때 우선순위 순으로 정렬 삽입
   - 현재: `list_push_back(&ready_list, &t->elem)` (FIFO)
   - 변경: `list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL)`

2. **`thread_yield()`**: ready_list에 넣을 때 우선순위 순으로 정렬 삽입
   - 현재: `list_push_back(&ready_list, &cur->elem)`
   - 변경: `list_insert_ordered(&ready_list, &cur->elem, cmp_priority, NULL)`

3. **`thread_set_priority()`**: 우선순위 변경 후, 현재 스레드보다 높은 우선순위 스레드가 있으면 yield
   - 현재: 단순히 `thread_current()->priority = new_priority`
   - 변경: 설정 후 `thread_yield()` 호출 여부 판단

4. **`thread_create()`**: 새로 생성된 스레드의 우선순위가 현재 스레드보다 높으면 즉시 yield (선점)

### `threads/synch.c` — 핵심 수정

1. **`sema_up()`**: waiters 리스트에서 **가장 높은 우선순위** 스레드를 깨워야 함
   - 현재: `list_pop_front` (FIFO)
   - 변경: `list_max` 또는 정렬 후 pop

2. **`sema_down()`**: waiters에 넣을 때 우선순위 순 정렬 (선택적)

3. **`cond_signal()`**: condition variable의 waiters에서 **가장 높은 우선순위** 세마포어를 시그널
   - 현재: `list_pop_front`
   - 변경: 세마포어 waiters 중 최고 우선순위 스레드를 가진 세마포어를 선택

---

## 테스트 5개 상세

### 1. priority-change (배점: 1)

**소스:** `priority-change.c`

**동작:**
1. main(priority=31) → thread 2 생성(priority=32) → 즉시 선점당함
2. thread 2가 자기 우선순위를 30으로 낮춤 → main(31)이 다시 실행됨
3. main이 자기 우선순위를 29로 낮춤 → thread 2(30)가 다시 실행됨

**기대 출력:**
```
Creating a high-priority thread 2.
Thread 2 now lowering priority.        ← thread 2(32)가 먼저 실행
Thread 2 should have just lowered its priority.  ← main(31)이 다시 실행
Thread 2 exiting.                      ← main(29)이 되자 thread 2(30)가 다시 실행
Thread 2 should have just exited.      ← main 마무리
```

**핵심 포인트:**
- `thread_create()` 시 선점(preemption) 구현 필요
- `thread_set_priority()` 시 현재 스레드보다 높은 ready 스레드가 있으면 yield

---

### 2. priority-preempt (배점: 1)

**소스:** `priority-preempt.c`

**동작:**
1. main(31) → "high-priority"(32) 스레드 생성
2. 생성 즉시 high-priority가 선점하여 5번 반복 + yield 후 종료
3. 그 후에야 main의 메시지 출력

**기대 출력:**
```
Thread high-priority iteration 0
Thread high-priority iteration 1
...
Thread high-priority iteration 4
Thread high-priority done!
The high-priority thread should have already completed.
```

**핵심 포인트:**
- `thread_create()`에서 새 스레드의 우선순위가 더 높으면 즉시 선점
- high-priority가 `thread_yield()`해도 ready_list에 main(31)밖에 없으므로 다시 자기가 실행됨

---

### 3. priority-fifo (배점: 1)

**소스:** `priority-fifo.c`

**동작:**
1. main이 자기 우선순위를 33으로 높임
2. 동일 우선순위(32)인 스레드 16개 생성
3. main이 우선순위를 31로 낮추면 16개 스레드가 실행 시작
4. 16개 스레드가 `thread_yield()`하며 16번씩 반복
5. **매 라운드마다 같은 순서**로 실행되어야 함

**기대 출력:**
```
iteration: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
iteration: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
... (16번 반복, 순서 동일)
```

**통과 조건:** 16개 라운드 모두 동일한 순서. 순서 자체는 0~15의 아무 순열이든 OK, 단 매번 같아야 함.

**핵심 포인트:**
- 같은 우선순위일 때 FIFO 순서가 보장되어야 함
- `thread_yield()` 시 ready_list **뒤쪽**에 넣어야 라운드 로빈이 유지됨
- `list_insert_ordered`는 같은 우선순위일 때 뒤쪽에 삽입하도록 비교 함수에서 **strictly greater** 비교를 사용해야 함

---

### 4. priority-sema (배점: 2)

**소스:** `priority-sema.c`

**동작:**
1. main이 우선순위를 PRI_MIN(0)으로 낮춤
2. 우선순위 21~30인 스레드 10개 생성, 각각 `sema_down()` 으로 블록
3. main이 10번 `sema_up()` → 매번 **가장 높은 우선순위** 스레드가 깨어남

**기대 출력:**
```
Thread priority 30 woke up.
Back in main thread.
Thread priority 29 woke up.
Back in main thread.
...
Thread priority 21 woke up.
Back in main thread.
```

**핵심 포인트:**
- `sema_up()`에서 waiters 리스트에서 최고 우선순위 스레드를 골라 깨워야 함
- 깨어난 스레드(21~30)가 main(0)보다 높으므로 즉시 실행됨
- 깨어난 스레드 종료 후 main으로 복귀 → "Back in main thread."

---

### 5. priority-condvar (배점: 2)

**소스:** `priority-condvar.c`

**동작:**
1. main이 우선순위를 PRI_MIN(0)으로 낮춤
2. 우선순위 21~30인 스레드 10개 생성, 각각 `cond_wait()` 으로 블록
3. main이 10번 `cond_signal()` → 매번 **가장 높은 우선순위** 스레드가 깨어남

**기대 출력:**
```
Thread priority 23 starting.    ← 생성 순서대로 starting 메시지
...
Thread priority 24 starting.
Signaling...
Thread priority 30 woke up.    ← signal 시 높은 순서대로 wakeup
Signaling...
Thread priority 29 woke up.
...
```

**핵심 포인트:**
- `cond_signal()`이 waiters(세마포어 리스트) 중에서 **가장 높은 우선순위 스레드를 가진 세마포어**를 선택해야 함
- `cond_wait()`의 내부 구조: 각 대기 스레드마다 별도의 세마포어(`semaphore_elem`)를 생성하여 `condition->waiters`에 넣음
- signal 시 이 세마포어들 중 대기 스레드의 우선순위가 가장 높은 것을 골라 `sema_up()`

---

## 구현 가이드

### 우선순위 비교 함수

```c
// threads/thread.c
bool cmp_priority(const struct list_elem *a,
                  const struct list_elem *b,
                  void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);
    return ta->priority > tb->priority;  // 높은 우선순위가 앞으로
}
```

> **주의:** `>=`가 아니라 `>`를 사용. 같은 우선순위일 때 뒤에 삽입되어야 FIFO 유지 (priority-fifo 통과 조건).

### thread_create() 선점 추가

```c
tid_t thread_create(...) {
    // ... 기존 코드 ...
    thread_unblock(t);

    // 새 스레드의 우선순위가 현재보다 높으면 선점
    if (t->priority > thread_current()->priority)
        thread_yield();

    return tid;
}
```

### thread_set_priority() 수정

```c
void thread_set_priority(int new_priority) {
    thread_current()->priority = new_priority;

    // ready_list에 나보다 높은 우선순위 스레드가 있으면 양보
    if (!list_empty(&ready_list)) {
        struct thread *front = list_entry(list_front(&ready_list),
                                           struct thread, elem);
        if (front->priority > new_priority)
            thread_yield();
    }
}
```

### sema_up() 수정

```c
void sema_up(struct semaphore *sema) {
    enum intr_level old_level = intr_disable();

    if (!list_empty(&sema->waiters)) {
        // 최고 우선순위 스레드를 찾아서 unblock
        list_sort(&sema->waiters, cmp_priority, NULL);
        thread_unblock(list_entry(list_pop_front(&sema->waiters),
                                   struct thread, elem));
    }
    sema->value++;

    // unblock한 스레드가 현재보다 높은 우선순위일 수 있으므로 yield 검사
    thread_yield();

    intr_set_level(old_level);
}
```

### cond_signal() 수정

```c
void cond_signal(struct condition *cond, struct lock *lock) {
    if (!list_empty(&cond->waiters)) {
        // waiters 중 가장 높은 우선순위 스레드를 가진 세마포어 선택
        list_sort(&cond->waiters, cmp_sema_priority, NULL);
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                             struct semaphore_elem, elem)->semaphore);
    }
}
```
