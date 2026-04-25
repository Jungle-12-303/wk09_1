# Phase 4: MLFQS (Multi-Level Feedback Queue Scheduler) 테스트 분석

## 목표

4.4BSD 스케줄러를 구현한다. `thread_mlfqs` 플래그가 `true`일 때 활성화되며, Phase 2~3의 우선순위 스케줄링/기부와 **독립적으로 동작**한다.

MLFQS에서는 우선순위를 프로그래머가 직접 설정하지 않고, **커널이 자동 계산**한다.

### 핵심 공식

**fixed-point 산술 (17.14 형식)** 을 사용해야 한다 (정수만으로 소수점 계산).

1. **priority** = `PRI_MAX - (recent_cpu / 4) - (nice × 2)`
   - 매 4 tick마다 현재 스레드에 대해 재계산

2. **recent_cpu** = `(2 × load_avg) / (2 × load_avg + 1) × recent_cpu + nice`
   - 매 1초(100 tick)마다 모든 스레드에 대해 재계산
   - 실행 중인 스레드는 매 tick마다 `recent_cpu += 1`

3. **load_avg** = `(59/60) × load_avg + (1/60) × ready_threads`
   - 매 1초(100 tick)마다 재계산
   - `ready_threads` = ready_list 스레드 수 + 실행 중 스레드(idle 제외)

## 수정 대상 파일

### `threads/thread.c` — 핵심 수정

1. `thread_tick()`: 매 tick마다 `recent_cpu++`, 매 4 tick마다 priority 재계산, 매 100 tick(1초)마다 load_avg/recent_cpu 재계산
2. `thread_set_nice()`, `thread_get_nice()`: nice 값 설정/반환
3. `thread_get_load_avg()`, `thread_get_recent_cpu()`: fixed-point → 정수 변환하여 반환 (×100)
4. `thread_set_priority()`: MLFQS 모드에서는 **무시** (자동 계산)

### `threads/thread.h` — 구조체 수정

```c
struct thread {
    int nice;          // nice 값 (-20 ~ +20)
    int recent_cpu;    // fixed-point 형식
};
```

### 새 파일: `threads/fixed-point.h` (또는 매크로)

```c
#define F (1 << 14)                          // 2^14 = 16384
#define INT_TO_FP(n) ((n) * F)               // 정수 → fixed-point
#define FP_TO_INT(x) ((x) / F)               // fixed-point → 정수 (버림)
#define FP_TO_INT_ROUND(x) ((x) >= 0 ? ((x) + F/2) / F : ((x) - F/2) / F)
#define FP_ADD(x, y) ((x) + (y))             // FP + FP
#define FP_SUB(x, y) ((x) - (y))             // FP - FP
#define FP_ADD_INT(x, n) ((x) + (n) * F)     // FP + 정수
#define FP_SUB_INT(x, n) ((x) - (n) * F)     // FP - 정수
#define FP_MUL(x, y) ((int64_t)(x) * (y) / F)  // FP × FP
#define FP_DIV(x, y) ((int64_t)(x) * F / (y))  // FP ÷ FP
#define FP_MUL_INT(x, n) ((x) * (n))         // FP × 정수
#define FP_DIV_INT(x, n) ((x) / (n))         // FP ÷ 정수
```

---

## 테스트 9개 상세

### 1. mlfqs-load-1 (배점: 1)

**소스:** `mlfqs-load-1.c`

**동작:**
1. 단일 스레드가 바쁘게 도는(spinning) 동안 `thread_get_load_avg()` 확인
2. 38~45초 사이에 load_avg가 0.5(×100 = 50)를 초과해야 함 (기대: 42초)
3. 이후 10초 sleep → load_avg가 0.5 아래로 떨어져야 함

**통과 조건:**
- 38~45초 범위 내에 load_avg > 0.50
- 10초 sleep 후 load_avg < 0.50

**핵심 포인트:** `load_avg = (59/60) × load_avg + (1/60) × 1`이 올바르게 계산되는지 검증. fixed-point 산술의 정확성이 핵심.

---

### 2. mlfqs-load-60 (배점: 1)

**소스:** `mlfqs-load-60.c`

**동작:**
1. nice=20인 스레드 60개가 10초 후 시작하여 60초간 spinning
2. 2초 간격으로 load_avg를 출력 (90회)
3. 60개가 running 상태일 때 load_avg가 ~38까지 상승
4. spinning 종료 후 감소

**통과 조건:** 기대 load_avg와 실제 값의 차이가 **3.5 이내**.

**핵심 포인트:** 대량의 스레드에서 load_avg가 정확하게 계산되는지 검증. timer_interrupt 내부 연산이 너무 오래 걸리면 실패.

---

### 3. mlfqs-load-avg (배점: 1)

**소스:** `mlfqs-load-avg.c`

**동작:**
1. 60개 스레드가 **시차를 두고**(스레드 i는 10+i초 후) spinning 시작
2. 각 스레드가 60초간 spinning 후 sleep
3. load_avg가 점진적으로 증가했다가 감소하는 곡선

**통과 조건:** 기대 load_avg와 차이 **2.5 이내**.

**핵심 포인트:** 시차 진입/퇴장 시 load_avg 곡선이 정확한지 검증. mlfqs-load-60보다 정밀도 요구가 높음.

---

### 4. mlfqs-recent-1 (배점: 1)

**소스:** `mlfqs-recent-1.c`

**동작:**
1. 단일 스레드가 180초간 spinning
2. 2초 간격으로 `recent_cpu`와 `load_avg` 출력

**통과 조건:** 기대 recent_cpu와 차이 **2.5 이내**.

**핵심 포인트:** `recent_cpu = (2×load_avg)/(2×load_avg+1) × recent_cpu + nice` 공식이 정확하게 계산되는지. recent_cpu가 초반에 빠르게 증가하다가 수렴하는 패턴.

---

### 5. mlfqs-fair-2 (배점: 1)

**소스:** `mlfqs-fair.c` → `test_mlfqs_fair(2, 0, 0)`

**동작:**
1. nice=0인 스레드 2개가 5초 대기 후 30초간 spinning
2. 각 스레드가 받은 tick 수 비교

**통과 조건:** 두 스레드의 tick 수 차이가 **50 이내**.

**핵심 포인트:** 같은 nice의 스레드는 거의 같은 양의 CPU를 받아야 함.

---

### 6. mlfqs-fair-20 (배점: 1)

**소스:** `mlfqs-fair.c` → `test_mlfqs_fair(20, 0, 0)`

**동작:** nice=0인 스레드 **20개**가 동일하게 실행.

**통과 조건:** 각 스레드의 tick 수와 기대값 차이가 **20 이내**.

**핵심 포인트:** 많은 스레드에서도 공정한 분배.

---

### 7. mlfqs-nice-2 (배점: 1)

**소스:** `mlfqs-fair.c` → `test_mlfqs_fair(2, 0, 5)`

**동작:**
1. 스레드 0 (nice=0)과 스레드 1 (nice=5)
2. 30초간 spinning

**통과 조건:** 기대값(시뮬레이션 계산)과 차이 **50 이내**.

**핵심 포인트:** nice가 높을수록 CPU를 덜 받아야 함. nice=0 ≈ 1904 ticks, nice=5 ≈ 1096 ticks.

---

### 8. mlfqs-nice-10 (배점: 1)

**소스:** `mlfqs-fair.c` → `test_mlfqs_fair(10, 0, 1)`

**동작:** nice=0 ~ nice=9인 스레드 10개.

**통과 조건:** 기대값과 차이 **25 이내**.

**핵심 포인트:** nice 값에 따른 CPU 분배 비율이 정확해야 함.

---

### 9. mlfqs-block (배점: 1)

**소스:** `mlfqs-block.c`

**동작:**
1. main이 lock 획득 후 25초 sleep
2. block 스레드가 20초 spinning 후 lock_acquire (10초간 블록됨)
3. main이 깨어나서 5초 spinning 후 lock 해제
4. block 스레드가 즉시 lock 획득해야 함

**기대 출력:**
```
Main thread acquiring lock.
Main thread creating block thread, sleeping 25 seconds...
Block thread spinning for 20 seconds...
Block thread acquiring lock...
Main thread spinning for 5 seconds...
Main thread releasing lock.
...got it.
Block thread should have already acquired lock.
```

**핵심 포인트:**
- 블록된(sleeping) 스레드의 recent_cpu는 증가하지 않아야 함
- 10초간 블록된 block 스레드의 recent_cpu가 충분히 감쇠하여, lock 해제 시 main보다 높은 우선순위를 가지고 즉시 실행되어야 함

---

## 구현 가이드

### thread_tick() 수정

```c
void thread_tick(void) {
    struct thread *t = thread_current();

    // ... 기존 tick 카운트 ...

    if (thread_mlfqs) {
        // 매 tick: 현재 스레드의 recent_cpu 증가 (idle이 아닌 경우)
        if (t != idle_thread)
            t->recent_cpu = FP_ADD_INT(t->recent_cpu, 1);

        // 매 1초 (100 tick): load_avg, 모든 스레드의 recent_cpu 재계산
        if (timer_ticks() % TIMER_FREQ == 0) {
            mlfqs_update_load_avg();
            mlfqs_update_recent_cpu_all();
        }

        // 매 4 tick: 현재 스레드의 priority 재계산
        if (timer_ticks() % 4 == 0)
            mlfqs_update_priority(t);
    }
}
```

### 주의사항

1. **MLFQS 모드에서 `thread_set_priority()`는 무시해야 함** — 직접 우선순위 설정 불가
2. **MLFQS 모드에서 priority donation은 비활성화** — `lock_acquire()`에서 기부하지 않음
3. **`thread_get_load_avg()`와 `thread_get_recent_cpu()`는 ×100 한 정수 반환** — fixed-point를 반올림하여 정수로 변환 후 100 곱함
4. **timer_interrupt 안에서 계산이 너무 오래 걸리면 안 됨** — mlfqs-load-avg 테스트가 이를 감지. 효율적인 구현 필요
5. **initial thread의 nice=0, recent_cpu=0** 으로 시작
