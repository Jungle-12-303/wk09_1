# Pintos 스레드 상태 전환 다이어그램

## 1. 스레드 상태 전환 (전체 흐름)

```mermaid
stateDiagram-v2
    direction LR

    [*] --> Ready : thread_create()

    Ready --> Running : schedule()<br/>ready_list에서 pop
    Running --> Ready : thread_yield()<br/>4틱마다 선점 (TIME_SLICE)
    Running --> Blocked : thread_block()<br/>sema_down / cond_wait / timer_sleep
    Blocked --> Ready : thread_unblock()<br/>sema_up / cond_signal / thread_awake
    Running --> Dying : thread_exit()
    Dying --> [*]

    note right of Ready
        ready_list (우선순위 내림차순 정렬)
        list_insert_ordered()로 삽입
        list_pop_front()로 꺼냄
    end note

    note right of Running
        thread_current()
        CPU를 점유 중인 유일한 스레드
    end note

    note left of Blocked
        sleep_list / sema.waiters / cond.waiters
        각각 다른 조건으로 대기
    end note

    note right of Dying
        schedule() 내부에서
        thread_launch() 대상에서 제외
    end note
```

## 2. Blocked 상태 세부 — 대기 큐별 진입/탈출

```mermaid
flowchart TB
    subgraph BLOCKED["🔒 Blocked 상태"]
        direction TB
        SL["sleep_list<br/><i>wakeup_tick 기준 대기</i>"]
        SW["sema.waiters<br/><i>lock_acquire / sema_down</i>"]
        CW["cond.waiters<br/><i>cond_wait → 내부 sema</i>"]
    end

    subgraph READY["✅ Ready 상태 (ready_list)"]
        RL["우선순위 내림차순 정렬"]
    end

    SL -- "thread_awake(ticks)<br/>매 tick마다 확인" --> RL
    SW -- "sema_up()<br/>lock_release 시 호출" --> RL
    CW -- "cond_signal()<br/>내부 sema_up" --> RL

    RUN["🟢 Running"] -- "timer_sleep()" --> SL
    RUN -- "sema_down() / lock_acquire()" --> SW
    RUN -- "cond_wait()" --> CW
```

## 3. timer_interrupt 구동 흐름

```mermaid
flowchart TD
    PIT["8254 PIT<br/>100Hz IRQ0"] --> IE["intr_entry<br/><i>레지스터 15개 저장</i>"]
    IE --> IH["intr_handler()"]
    IH --> TI["timer_interrupt()"]

    TI --> T1["ticks++"]
    TI --> T2["thread_tick()"]
    TI --> T3["thread_awake(ticks)"]

    T2 --> T2A{"thread_ticks >= 4?"}
    T2A -- "Yes" --> YIELD_FLAG["intr_yield_on_return()<br/><i>플래그만 세팅</i>"]
    T2A -- "No" --> PASS1["pass"]

    T3 --> T3A{"wakeup_tick ≤ ticks?"}
    T3A -- "Yes" --> UNBLOCK["thread_unblock()<br/><i>Blocked → Ready</i>"]
    T3A -- "No" --> PASS2["pass"]

    IH --> EOI["pic_end_of_interrupt()<br/><i>EOI 전송</i>"]
    EOI --> CHECK{"yield_on_return?"}
    CHECK -- "Yes" --> TY["thread_yield()<br/><i>Running → Ready</i>"]
    CHECK -- "No" --> IRETQ["iretq<br/><i>원래 코드로 복귀</i>"]
    TY --> SCHED["schedule()<br/><i>다음 스레드 선택</i>"]
```

## 4. Priority Donation 흐름

```mermaid
flowchart LR
    subgraph LOCK_ACQUIRE["lock_acquire()"]
        direction TB
        A1["holder 있는가?"] -- "Yes" --> A2["curr→waiting_lock = lock"]
        A2 --> A3["holder→donations에 자신 추가"]
        A3 --> A4["holder→priority를<br/>자신의 priority로 올림"]
        A4 --> A5["sema_down() → Blocked"]
        A1 -- "No" --> A6["lock→holder = curr<br/>락 획득 성공"]
    end

    subgraph LOCK_RELEASE["lock_release()"]
        direction TB
        R1["donations에서<br/>이 락 기다리던 스레드 제거"]
        R1 --> R2["refresh_priority()<br/><i>남은 기부자 중 최대 vs origin</i>"]
        R2 --> R3["holder = NULL"]
        R3 --> R4["sema_up() → 대기자 unblock"]
        R4 --> R5["check_preemption()"]
    end

    LOCK_ACQUIRE --> LOCK_RELEASE
```

## ASCII 버전 (Mermaid 미지원 환경용)

```
                        thread_create()
                              │
                              ▼
┌──────────┐  schedule()  ┌──────────┐  thread_exit()  ┌──────────┐
│  Create  │────────────▶│  Ready   │───────────────▶│  Dying   │
└──────────┘             │(ready_  │                 └──────────┘
                          │  list)  │                       ▲
                          └────┬────┘                       │
                               │                      thread_exit()
                    schedule() │                            │
                               ▼                            │
                    ┌─────────────────────┐                 │
     thread_yield() │                     │                 │
     Time-out/선점  │      Running        │─────────────────┘
          ┌─────────│  thread_current()   │
          │         │                     │
          │         └──────────┬──────────┘
          │                    │
          │         thread_block()
          │                    │
          │                    ▼
          │         ┌─────────────────────┐
          │         │                     │
          │         │      Blocked        │
          │         │  sleep/sema/cond    │
          │         │                     │
          │         └──────────┬──────────┘
          │                    │
          │         thread_unblock()
          │         sema_up / thread_awake
          │                    │
          ▼                    ▼
        ┌──────────────────────────┐
        │          Ready           │
        │   (우선순위 내림차순 정렬)  │
        └──────────────────────────┘
```

## Blocked 세부 대기 큐

```
                    ┌─────────────────────┐
                    │      Blocked        │
                    └─────────┬───────────┘
                              │
              ┌───────────────┼───────────────┐
              │               │               │
              ▼               ▼               ▼
     ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
     │  sleep_list  │ │sema.waiters  │ │cond.waiters  │
     │              │ │              │ │              │
     │thread_awake()│ │  sema_up()   │ │cond_signal() │
     └──────────────┘ └──────────────┘ └──────────────┘
```

## timer_interrupt가 구동하는 전환

```
★ timer_interrupt() — 매 tick (100Hz) 마다 호출

  ├── ticks++
  ├── thread_tick()
  │     └── 4틱마다 → intr_yield_on_return()  ← Running → Ready 전환 유발
  └── thread_awake(ticks)
        └── wakeup_tick ≤ ticks인 스레드 → thread_unblock()  ← Blocked → Ready 전환

→ 이 두 경로가 스레드 상태 전환의 원동력
```
