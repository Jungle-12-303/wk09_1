# 버그 시퀀스 다이어그램

## 버그: check_preemption — Page Fault + EOI 문제

하나의 함수에 두 가지 버그가 동시에 존재했다.

---

### 문제 1: 빈 ready_list 접근 → Page Fault

```mermaid
sequenceDiagram
    participant main as main()
    participant ti as thread_init()
    participant tc as thread_create("idle")
    participant cp as check_preemption()
    participant rl as ready_list
    participant ii as intr_init()
    participant ts as thread_start()

    Note over main: Pintos 부팅 시작
    main->>ti: 호출
    ti->>rl: list_init(&ready_list)
    Note over rl: ready_list = 비어있음<br/>(head ↔ tail sentinel만 존재)
    ti-->>main: main 스레드 생성 완료

    main->>tc: thread_create("idle", PRI_MIN, idle, NULL)
    Note over tc: idle 스레드를 만든다
    tc->>cp: check_preemption()
    Note over cp: ready_list에서 최고 우선순위를<br/>확인하려 함

    cp->>rl: list_front(&ready_list)
    Note over rl: ⚠️ 리스트가 비어있음!<br/>head sentinel 반환

    rl-->>cp: &head (sentinel 노드)
    Note over cp: list_entry()로<br/>struct thread * 캐스팅
    Note over cp: front→priority 접근 시도
    Note over cp: ❌ Page Fault!<br/>sentinel은 thread가 아니다<br/>유효하지 않은 메모리 주소

    Note over ii,ts: intr_init(), timer_init(),<br/>thread_start()는<br/>아직 호출되지도 않았다
```

### 문제 2: 인터럽트 안에서 직접 yield → 시스템 멈춤

```mermaid
sequenceDiagram
    participant pit as 8254 PIT
    participant cpu as CPU
    participant ih as intr_handler()
    participant ti as timer_interrupt()
    participant tt as thread_tick()
    participant cp as check_preemption()
    participant pic as PIC (8259A)

    pit->>cpu: IRQ0 (100Hz)
    Note over cpu: 인터럽트 진입<br/>인터럽트 OFF 상태

    cpu->>ih: intr_handler(frame)
    ih->>ti: timer_interrupt()
    ti->>tt: thread_tick()
    Note over tt: thread_ticks >= 4 (TIME_SLICE)
    tt->>cp: check_preemption()

    Note over cp: ready_list에 더 높은<br/>우선순위 스레드 있음

    cp->>cp: thread_yield() 직접 호출!
    Note over cp: ❌ 컨텍스트 스위치 발생!<br/>다른 스레드로 전환됨

    Note over pic: ⚠️ EOI를 아직 안 받았다!<br/>"이전 인터럽트 처리 중"으로 인식
    Note over pic: 다음 IRQ0 차단됨
    Note over pit: 타이머 계속 울리지만<br/>PIC가 CPU에 전달 안 함
    Note over cpu: ❌ 시스템 멈춤!<br/>타이머 인터럽트가<br/>영원히 오지 않는다
```

### 수정 후: 두 문제를 한 함수에서 동시 해결

```mermaid
sequenceDiagram
    participant ih as intr_handler()
    participant ti as timer_interrupt()
    participant cp as check_preemption()
    participant pic as PIC (8259A)
    participant sched as schedule()

    ih->>ti: timer_interrupt()
    ti->>cp: check_preemption()

    Note over cp: ① list_empty 가드<br/>→ 빈 리스트면 return (Page Fault 방지)

    Note over cp: ② intr_context() 확인<br/>→ 인터럽트 안이면<br/>직접 yield 대신 플래그만 세팅
    cp->>cp: intr_yield_on_return()
    Note over cp: ✅ yield_on_return = true<br/>(아직 yield 안 함!)
    cp-->>ti: 정상 복귀
    ti-->>ih: 정상 복귀

    Note over ih: intr_handler 마무리 단계
    ih->>pic: pic_end_of_interrupt()
    Note over pic: ✅ EOI 수신 완료<br/>다음 IRQ0 허용됨

    ih->>ih: yield_on_return == true?
    ih->>sched: thread_yield() → schedule()
    Note over sched: ✅ EOI 이후에 안전하게<br/>컨텍스트 스위치
```

### 핵심: 부팅 순서와 인터럽트 처리 순서

```mermaid
flowchart LR
    A["thread_init()<br/><i>ready_list 초기화</i><br/><i>main 스레드 생성</i>"] --> B["thread_create(idle)<br/><i>⚠️ ready_list 비어있음</i><br/><i>→ Page Fault</i>"]
    B --> C["intr_init()<br/><i>IDT 설정</i>"]
    C --> D["timer_init()<br/><i>8254 PIT 설정</i>"]
    D --> E["thread_start()<br/><i>인터럽트 ON</i>"]

    style B fill:#FEE2E2,stroke:#DC2626,stroke-width:2px
    style E fill:#D1FAE5,stroke:#059669,stroke-width:2px
```

```mermaid
flowchart LR
    F["timer_interrupt()"] --> G["check_preemption()"]
    G --> H{"intr_context()?"}
    H -- "Yes" --> I["intr_yield_on_return()<br/><i>플래그만 세팅</i>"]
    I --> J["pic_end_of_interrupt()<br/><i>EOI 전송</i>"]
    J --> K["thread_yield()<br/><i>안전한 스위치</i>"]
    H -- "No" --> L["thread_yield()<br/><i>바로 스위치</i>"]

    style I fill:#D1FAE5,stroke:#059669,stroke-width:2px
    style J fill:#DBEAFE,stroke:#2563EB,stroke-width:2px
    style K fill:#D1FAE5,stroke:#059669,stroke-width:2px
```

### 이 함수 하나에 담긴 이해

| 해결한 문제 | 필요한 이해 |
|-----------|----------|
| `list_empty` 가드 | Pintos 부팅 순서 — idle 생성 시점에 ready_list가 비어있다 |
| `intr_context` 분기 | 인터럽트 핸들러는 특수한 상태 — EOI 전에 스위치하면 안 된다 |
| `intr_yield_on_return` | Pintos의 지연 yield 설계 — 핸들러 밖에서 안전하게 yield |

---

## 버그 2: thread_set_priority에서 우선순위가 안 내려감

### 발생 상황 — 수정 전 코드

```mermaid
sequenceDiagram
    participant user as 테스트 코드
    participant sp as thread_set_priority()
    participant t as curr (현재 스레드)

    Note over t: priority = 32<br/>(acquire1에게 기부받은 상태)

    user->>sp: thread_set_priority(31)
    sp->>t: new_priority(31) > curr→priority(32)?
    Note over t: 31 > 32 → false
    Note over sp: ❌ 아무것도 안 함!
    sp-->>user: priority 여전히 32

    Note over user: 기부가 끝나도<br/>원래 우선순위로<br/>돌아가지 않는다

    user->>sp: thread_set_priority(40)
    sp->>t: new_priority(40) > curr→priority(32)?
    Note over t: 40 > 32 → true
    sp->>t: curr→priority = 40
    Note over sp: ✅ 올라가는 건 반영됨

    Note over user: 올리는 건 되는데<br/>내리는 건 안 된다 → 버그
```

### 수정 후 — origin_priority + refresh_priority

```mermaid
sequenceDiagram
    participant user as 테스트 코드
    participant sp as thread_set_priority()
    participant rp as refresh_priority()
    participant t as curr (현재 스레드)
    participant dl as donations 리스트

    Note over t: origin_priority = 31<br/>priority = 32<br/>(acquire1이 기부 중)

    user->>sp: thread_set_priority(20)
    sp->>t: curr→origin_priority = 20
    sp->>rp: refresh_priority(curr)

    rp->>t: priority = origin_priority (20)
    rp->>dl: donations 리스트 순회
    dl-->>rp: acquire1 (priority=32)
    rp->>t: 32 > 20 → priority = 32
    Note over t: origin = 20, priority = 32<br/>기부가 아직 있으므로 32 유지

    Note over user: lock_release 후...
    Note over t: donations에서 acquire1 제거

    user->>sp: refresh_priority(curr) 재호출
    rp->>t: priority = origin_priority (20)
    rp->>dl: donations 리스트 순회
    dl-->>rp: (비어있음)
    Note over t: origin = 20, priority = 20<br/>✅ 기부 끝나면 원래 값으로 복귀
```

### Priority Donation과의 연결

```mermaid
flowchart TB
    subgraph BEFORE["❌ 수정 전"]
        B1["thread_set_priority(20)"] --> B2{"20 > 32?"}
        B2 -- "false" --> B3["무시됨<br/>priority = 32 유지"]
    end

    subgraph AFTER["✅ 수정 후"]
        A1["thread_set_priority(20)"] --> A2["origin_priority = 20"]
        A2 --> A3["refresh_priority()"]
        A3 --> A4{"donations 있나?"}
        A4 -- "있음: 기부자 max=32" --> A5["priority = max(20, 32) = 32<br/><i>기부 유지</i>"]
        A4 -- "없음" --> A6["priority = 20<br/><i>원래 값 복귀</i>"]
    end

    style BEFORE fill:#FEF2F2,stroke:#DC2626
    style AFTER fill:#F0FDF4,stroke:#059669
```
