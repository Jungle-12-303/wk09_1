# 현재 구현 기준 스레드 상태 전이

현재 구현 기준 공식 상태는 아래 4개입니다.

- `THREAD_READY`
- `THREAD_RUNNING`
- `THREAD_BLOCKED`
- `THREAD_DYING`

주의:

- `sleep`은 별도 enum 상태가 아니라 `sleep_q`에 들어간 `THREAD_BLOCKED`의 특수한 경우입니다.
- 발표에서는 이해를 위해 `BLOCKED(sync wait)`와 `BLOCKED(sleep queue)`로 나눠 그렸습니다.

## Mermaid 상태도

```mermaid
flowchart LR
    C[Create]
    R[READY]
    N[RUNNING]
    B1["BLOCKED<br/>(sync wait)"]
    B2["BLOCKED<br/>(sleep queue)"]
    D[DYING]

    C -->|"thread_create()<br/>thread_unblock()"| R
    R -->|"schedule()<br/>next_thread_to_run()"| N

    N -->|"thread_yield()<br/>check_preemption()<br/>timer interrupt -> intr_yield_on_return()"| R

    N -->|"sema_down()<br/>lock_acquire() 대기<br/>cond_wait()"| B1
    B1 -->|"sema_up()<br/>lock_release()<br/>cond_signal()"| R

    N -->|"timer_sleep()<br/>thread_sleep()"| B2
    B2 -->|"timer_interrupt()<br/>thread_awake()<br/>thread_unblock()"| R

    N -->|"thread_exit()"| D

    classDef ready fill:#EEF6FF,stroke:#4C8FD6,stroke-width:2px,color:#2B5DA8;
    classDef running fill:#ECFDF5,stroke:#19C37D,stroke-width:2px,color:#138A59;
    classDef blocked1 fill:#FFF7E8,stroke:#F59E0B,stroke-width:2px,color:#B46900;
    classDef blocked2 fill:#F7F0FF,stroke:#9B6BDF,stroke-width:2px,color:#7441C7;
    classDef dying fill:#FFF0F2,stroke:#E76F7A,stroke-width:2px,color:#C84C5C;
    classDef create fill:#F8FAFC,stroke:#CBD5E1,stroke-width:2px,color:#334155;

    class C create;
    class R ready;
    class N running;
    class B1 blocked1;
    class B2 blocked2;
    class D dying;
```

## 발표용 한 줄 설명

> 현재 구현에서는 `READY`, `RUNNING`, `BLOCKED`, `DYING` 네 상태만 실제로 존재하고,  
> `sleep`은 별도 상태가 아니라 타이머로 깨어나는 `BLOCKED`의 한 종류입니다.

## Mermaid 상세 버전

아래 버전은 GitHub에서 그대로 렌더링하기 좋은 `flowchart` 기반 상세 상태도입니다.

```mermaid
flowchart TB
    start([thread_create 호출])
    init["init_thread()<br/>status = THREAD_BLOCKED"]
    unblock["thread_unblock(t)<br/>ready_list_push(t)<br/>status = THREAD_READY"]
    ready["THREAD_READY<br/>ready_list에 존재"]
    schedule["schedule()<br/>next_thread_to_run()"]
    running["THREAD_RUNNING<br/>현재 CPU 사용 중"]

    tick["timer_interrupt()<br/>ticks++<br/>thread_tick()<br/>thread_awake()"]
    preempt["intr_yield_on_return()<br/>또는 thread_yield()"]
    yield["thread_yield()<br/>do_schedule(THREAD_READY)"]

    sync_wait["동기화 대기 진입<br/>sema_down()<br/>lock_acquire() 대기<br/>cond_wait()"]
    blocked_sync["THREAD_BLOCKED<br/>sync waiters에 존재"]
    sync_wake["sema_up()<br/>lock_release()<br/>cond_signal()"]

    sleep_enter["timer_sleep()<br/>thread_sleep(wakeup_tick)"]
    sleep_queue["sleep_q 삽입<br/>wakeup_tick 기록"]
    blocked_sleep["THREAD_BLOCKED<br/>sleep_q에 존재"]
    awake["thread_awake(current_tick)<br/>wakeup_tick <= ticks 검사"]

    exit["thread_exit()<br/>do_schedule(THREAD_DYING)"]
    dying["THREAD_DYING"]

    start --> init
    init --> unblock
    unblock --> ready

    ready --> schedule
    schedule --> running

    running --> tick
    tick -->|"TIME_SLICE 초과 또는<br/>더 높은 READY 존재"| preempt
    preempt --> yield
    yield --> ready

    running -->|"명시적 양보"| yield

    running --> sync_wait
    sync_wait -->|"thread_block()"| blocked_sync
    blocked_sync --> sync_wake
    sync_wake -->|"thread_unblock()"| ready

    running --> sleep_enter
    sleep_enter --> sleep_queue
    sleep_queue -->|"thread_block()"| blocked_sleep
    blocked_sleep -->|"timer interrupt 발생"| awake
    awake -->|"thread_unblock()"| ready

    running --> exit
    exit --> dying

    note1["참고 1<br/>sleep도 별도 enum 상태가 아니라<br/>코드상으로는 THREAD_BLOCKED"]
    note2["참고 2<br/>preemption은 즉시 thread_yield()가 아니라<br/>intr_context()면 intr_yield_on_return() 사용"]
    note3["참고 3<br/>READY -> RUNNING 전환은 항상 schedule() 경유"]

    blocked_sleep -.-> note1
    preempt -.-> note2
    schedule -.-> note3

    classDef create fill:#F8FAFC,stroke:#CBD5E1,stroke-width:2px,color:#334155;
    classDef ready fill:#EEF6FF,stroke:#4C8FD6,stroke-width:2px,color:#2B5DA8;
    classDef running fill:#ECFDF5,stroke:#19C37D,stroke-width:2px,color:#138A59;
    classDef blocked1 fill:#FFF7E8,stroke:#F59E0B,stroke-width:2px,color:#B46900;
    classDef blocked2 fill:#F7F0FF,stroke:#9B6BDF,stroke-width:2px,color:#7441C7;
    classDef dying fill:#FFF0F2,stroke:#E76F7A,stroke-width:2px,color:#C84C5C;
    classDef note fill:#FFFFFF,stroke:#D1D5DB,stroke-dasharray: 5 5,color:#4B5563;

    class start,init,unblock,preempt,awake,sync_wake,sleep_enter,sleep_queue,sync_wait,tick,exit,create create;
    class ready,schedule,yield ready;
    class running running;
    class blocked_sync blocked1;
    class blocked_sleep blocked2;
    class dying dying;
    class note1,note2,note3 note;
```

## 발표용 설명 포인트

- `thread_create()` 직후 스레드는 바로 `RUNNING`이 아니라, 먼저 `THREAD_READY`로 `ready_list`에 들어갑니다.
- `READY -> RUNNING` 전환은 항상 `schedule()`을 거칩니다.
- `sleep`과 `sync wait`는 논리적으로 다르지만, 코드상 상태값은 둘 다 `THREAD_BLOCKED`입니다.
- 타이머 인터럽트는 단순히 시간을 올리는 게 아니라 `thread_tick()`과 `thread_awake()`를 통해 선점과 wakeup을 동시에 담당합니다.
- 인터럽트 컨텍스트 안에서는 곧바로 `thread_yield()`하지 않고 `intr_yield_on_return()`로 복귀 직후 스케줄링을 예약합니다.
