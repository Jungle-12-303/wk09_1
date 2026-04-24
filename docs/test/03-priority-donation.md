# Phase 3: Priority Donation 테스트 분석

## 목표

**우선순위 역전(Priority Inversion)** 문제를 해결한다.

문제 상황: 낮은 우선순위 스레드 L이 락을 잡고 있고, 높은 우선순위 스레드 H가 그 락을 기다리면, 중간 우선순위 스레드 M이 L보다 먼저 실행되어 H가 무한 대기하는 현상.

해결: H가 자기 우선순위를 L에게 **기부(donate)** 하여 L이 빨리 락을 해제하도록 한다.

## 수정 대상 파일

### `threads/thread.h` — 구조체 수정

```c
struct thread {
    int priority;           // 유효 우선순위 (기부 반영)
    int base_priority;      // 원래 우선순위 (기부 전)
    struct lock *wait_on_lock;   // 이 스레드가 대기 중인 락
    struct list donations;       // 이 스레드에게 기부한 스레드 리스트
    struct list_elem donation_elem;  // donations 리스트용 elem
};
```

### `threads/synch.c` — 핵심 수정

1. **`lock_acquire()`**: 락 holder에게 우선순위 기부
2. **`lock_release()`**: 기부 제거, 우선순위 복원
3. `sema_up()`, `sema_down()`: 우선순위 순 정렬 (Phase 2에서 이미 수정)

### `threads/thread.c` — 핵심 수정

1. **`thread_set_priority()`**: base_priority 변경 + 기부 우선순위와 비교하여 유효 우선순위 결정

---

## 테스트 7개 상세

### 1. priority-donate-one (배점: 2)

**소스:** `priority-donate-one.c`

**시나리오:**
```
main(31) → lock 획득
         → acquire1(32) 생성 → lock 대기 → main에게 32 기부
         → acquire2(33) 생성 → lock 대기 → main에게 33 기부
         → main이 lock 해제
         → acquire2(33)가 먼저 lock 획득 → 완료
         → acquire1(32)가 lock 획득 → 완료
         → main 마무리
```

**기대 출력:**
```
This thread should have priority 32.  Actual priority: 32.
This thread should have priority 33.  Actual priority: 33.
acquire2: got the lock
acquire2: done
acquire1: got the lock
acquire1: done
acquire2, acquire1 must already have finished, in that order.
This should be the last line before finishing this test.
```

**핵심 포인트:**
- `lock_acquire()` 시 holder의 priority를 자신의 priority로 올려야 함
- `thread_get_priority()`가 기부받은 우선순위를 반환해야 함
- lock 해제 시 우선순위 순(33→32)으로 락 획득

---

### 2. priority-donate-multiple (배점: 3)

**소스:** `priority-donate-multiple.c`

**시나리오:** main이 **2개의 락(A, B)** 을 보유. 각 락에 대해 서로 다른 스레드가 기부.

```
main(31) → lock A, B 획득
         → thread a(32) 생성 → lock A 대기 → main에게 32 기부
         → thread b(33) 생성 → lock B 대기 → main에게 33 기부
         → main의 유효 우선순위 = 33 (최대값)
         → lock B 해제 → b 실행 완료
         → main의 유효 우선순위 = 32 (a의 기부만 남음)
         → lock A 해제 → a 실행 완료
         → main의 유효 우선순위 = 31 (원래값)
```

**기대 출력:**
```
Main thread should have priority 32.  Actual priority: 32.
Main thread should have priority 33.  Actual priority: 33.
Thread b acquired lock b.
Thread b finished.
Thread b should have just finished.
Main thread should have priority 32.  Actual priority: 32.
Thread a acquired lock a.
Thread a finished.
Thread a should have just finished.
Main thread should have priority 31.  Actual priority: 31.
```

**핵심 포인트:**
- 여러 기부를 동시에 관리해야 함 (donations 리스트)
- 특정 락 해제 시 **그 락에 대한 기부만** 제거
- 남은 기부 중 최대값으로 우선순위 복원

---

### 3. priority-donate-multiple2 (배점: 3)

**소스:** `priority-donate-multiple2.c`

**시나리오:** multiple과 비슷하나 **락 해제 순서가 다름** + **관계없는 스레드 c** 추가.

```
main(31) → lock A, B 획득
         → thread a(34) 생성 → lock A 대기 → main에게 34 기부
         → thread c(32) 생성 → 락 안 기다림 (바로 ready)
         → thread b(36) 생성 → lock B 대기 → main에게 36 기부
         → lock A 해제 (B가 아니라 A를 먼저!)
           → a가 깨어나지만 main(36) > a(34)이므로 main 계속 실행
           → main의 유효 우선순위 = 36 (b의 기부 유지)
         → lock B 해제
           → b(36) 실행 → a(34) 실행 → c(32) 실행 → main(31)
```

**기대 출력:**
```
Main thread should have priority 34.  Actual priority: 34.
Main thread should have priority 36.  Actual priority: 36.
Main thread should have priority 36.  Actual priority: 36.   ← A 해제 후에도 B 기부 유지
Thread b acquired lock b.
Thread b finished.
Thread a acquired lock a.
Thread a finished.
Thread c finished.
Threads b, a, c should have just finished, in that order.
Main thread should have priority 31.  Actual priority: 31.
```

**핵심 포인트:**
- 락 해제 시 **해당 락에 대한 기부만** 정확히 제거
- 다른 락에 의한 기부는 유지

---

### 4. priority-donate-nest (배점: 3)

**소스:** `priority-donate-nest.c`

**시나리오: 중첩(Nested) 기부**

```
L(31) → lock A 획득
M(32) → lock B 획득 → lock A 대기 → L에게 32 기부
H(33) → lock B 대기 → M에게 33 기부 → M이 L에게 33 전파!
```

```
L의 유효 우선순위: 31 → 32 → 33 (H의 기부가 M을 거쳐 L까지 전파)
M의 유효 우선순위: 32 → 33 (H의 직접 기부)
```

**기대 출력:**
```
Low thread should have priority 32.  Actual priority: 32.
Low thread should have priority 33.  Actual priority: 33.
Medium thread should have priority 33.  Actual priority: 33.
Medium thread got the lock.
High thread got the lock.
High thread finished.
High thread should have just finished.
Middle thread finished.
Medium thread should just have finished.
Low thread should have priority 31.  Actual priority: 31.
```

**핵심 포인트:**
- 기부가 **체인(chain)** 으로 전파되어야 함
- A → B → C: A가 B에게 기부하면, B가 또 다른 락을 기다리고 있다면 B의 holder(C)에게도 전파
- `lock_acquire()` 시 `wait_on_lock` 필드를 따라가며 재귀적으로 기부

---

### 5. priority-donate-chain (배점: 3)

**소스:** `priority-donate-chain.c`

**시나리오: 8단계 깊이의 체인 기부**

```
main(0) → lock[0] 획득
thread 1(3)  → lock[1] 획득 → lock[0] 대기 → main에게 3 기부
thread 2(6)  → lock[2] 획득 → lock[1] 대기 → thread 1에게 6 기부 → main에게 6 전파
...
thread 7(21) → lock[6] 대기 → ... → main에게 21 전파
```

각 단계에서 `interloper` 스레드(priority - 1)도 생성되어, 체인 기부가 올바르면 interloper보다 기부받은 스레드가 먼저 실행됨을 검증.

**기대 출력:**
```
main should have priority 3.  Actual priority: 3.
main should have priority 6.  Actual priority: 6.
...
main should have priority 21.  Actual priority: 21.
thread 1 got lock
thread 1 should have priority 21. Actual priority: 21
...
thread 7 finishing with priority 21.
interloper 7 finished.
thread 6 finishing with priority 18.
interloper 6 finished.
...
main finishing with priority 0.
```

**핵심 포인트:**
- 깊이 제한 없는 체인 기부 전파
- 기부 해제 시 연쇄적으로 우선순위 복원
- interloper가 해당 thread보다 늦게 실행되어야 함

---

### 6. priority-donate-sema (배점: 2)

**소스:** `priority-donate-sema.c`

**시나리오: 기부 + 세마포어 조합**

```
L(32) → lock 획득 → sema_down (블록)
M(34) → sema_down (블록)
H(36) → lock_acquire 시도 → L에게 36 기부 → 블록
main(31) → sema_up
  → L이 깨어남 (기부받은 36이라 M(34)보다 높으므로)
  → L이 lock 해제 → H(36) 실행
  → H가 sema_up → M(34) 실행
```

**기대 출력:**
```
Thread L acquired lock.
Thread L downed semaphore.
Thread H acquired lock.
Thread H finished.
Thread M finished.
Thread L finished.
Main thread finished.
```

**핵심 포인트:**
- 세마포어와 락의 상호작용
- sema_up 시 우선순위 높은 스레드를 깨워야 함 (Phase 2에서 이미 처리)
- 기부받은 우선순위가 세마포어 wakeup 순서에 영향

---

### 7. priority-donate-lower (배점: 2)

**소스:** `priority-donate-lower.c`

**시나리오: 기부 중 base priority 낮추기**

```
main(31) → lock 획득
         → acquire(41) 생성 → lock 대기 → main에게 41 기부
         → main의 유효 우선순위 = 41
         → main이 thread_set_priority(21) 호출
           → base_priority = 21이지만, 기부 41이 유지되므로 유효 = 41
         → lock 해제 → acquire 실행
         → 기부 제거 → main의 유효 우선순위 = 21 (base)
```

**기대 출력:**
```
Main thread should have priority 41.  Actual priority: 41.
Lowering base priority...
Main thread should have priority 41.  Actual priority: 41.   ← 기부 유지!
acquire: got the lock
acquire: done
acquire must already have finished.
Main thread should have priority 21.  Actual priority: 21.   ← base로 복원
```

**핵심 포인트:**
- `thread_set_priority()`는 `base_priority`만 변경
- 기부가 활성화되어 있으면 유효 우선순위는 max(base, 기부) 유지
- 기부 제거 시 `base_priority`로 복원

---

## 구현 가이드

### lock_acquire() 수정

```c
void lock_acquire(struct lock *lock) {
    struct thread *cur = thread_current();

    if (lock->holder != NULL) {
        // 현재 스레드가 기다리는 락 기록
        cur->wait_on_lock = lock;

        // 기부: holder에게 내 우선순위 전파
        struct lock *l = lock;
        while (l && l->holder) {
            if (l->holder->priority < cur->priority)
                l->holder->priority = cur->priority;
            l = l->holder->wait_on_lock;  // 체인 전파
        }

        // donations 리스트에 추가
        list_insert_ordered(&lock->holder->donations,
                            &cur->donation_elem, cmp_priority_donation, NULL);
    }

    sema_down(&lock->semaphore);

    cur->wait_on_lock = NULL;
    lock->holder = cur;
}
```

### lock_release() 수정

```c
void lock_release(struct lock *lock) {
    struct thread *cur = thread_current();

    // 이 락에 대한 기부 제거
    struct list_elem *e = list_begin(&cur->donations);
    while (e != list_end(&cur->donations)) {
        struct thread *t = list_entry(e, struct thread, donation_elem);
        if (t->wait_on_lock == lock)
            e = list_remove(e);
        else
            e = list_next(e);
    }

    // 남은 기부 중 최대값으로 우선순위 복원
    cur->priority = cur->base_priority;
    if (!list_empty(&cur->donations)) {
        struct thread *front = list_entry(list_front(&cur->donations),
                                           struct thread, donation_elem);
        if (front->priority > cur->priority)
            cur->priority = front->priority;
    }

    lock->holder = NULL;
    sema_up(&lock->semaphore);
}
```

### thread_set_priority() 수정

```c
void thread_set_priority(int new_priority) {
    struct thread *cur = thread_current();
    cur->base_priority = new_priority;

    // 기부가 없거나 base가 더 높으면 반영
    if (list_empty(&cur->donations) || new_priority > cur->priority)
        cur->priority = new_priority;

    // 더 높은 우선순위 스레드가 있으면 양보
    thread_yield();
}
```

### 체인 기부 깊이 제한

Pintos 공식 가이드에서는 체인 깊이를 8단계로 제한할 것을 권장. `priority-donate-chain`이 정확히 8단계이므로 이 제한으로 충분하다.
