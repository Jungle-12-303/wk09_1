---
type: Knowledge
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
  - layer:cpu
  - layer:kernel
  - layer:emulator
  - topic:interrupt
  - topic:scheduler
related_to:
  - "[[week-1-threads-map]]"
---

# 인터럽트와 타이머

## 작은 질문

프로그램이 끝나지 않고 계속 실행 중이면, 운영체제는 어떻게 다시 CPU를 되찾을까?

답은 타이머 인터럽트다. CPU가 유저 코드나 커널 코드를 실행하고 있어도, 주기적으로 타이머 이벤트가 들어오면 운영체제가 잠깐 제어권을 얻고 스케줄링 결정을 할 수 있다.

## 핵심 모델

인터럽트는 현재 실행 흐름을 잠시 멈추고, CPU가 미리 등록된 handler로 이동하게 만드는 이벤트다.

timer interrupt는 OS가 주기적으로 CPU를 되찾아 scheduling 결정을 내릴 수 있게 한다.

## Linux / Windows에서는

실제 OS는 APIC, interrupt descriptor table, interrupt priority, SMP, device driver, deferred work를 함께 다룬다.

Linux와 Windows 모두 timer interrupt를 time accounting, preemption, sleep timeout, scheduling에 사용하지만 실제 구현은 훨씬 복잡하다.

## PintOS에서는

PintOS에서는 timer interrupt가 thread scheduling 학습의 중심이다.

대표 파일:

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/interrupt.c`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/devices/timer.c`
- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/thread.c`

흐름:

```text
timer interrupt
  -> interrupt handler dispatch
  -> timer_interrupt
  -> thread_tick
  -> intr_yield_on_return if time slice expired
  -> interrupt return
  -> schedule
```

### 왜 `intr_yield_on_return()`로 “예약”할까?

처음 보면 “time slice가 끝났으면 `thread_yield()`나 `schedule()`을 바로 호출하면 되지 않나?”라는 의문이 든다.

PintOS는 **외부 인터럽트 문맥(external interrupt context)**에서 바로 스케줄링하지 않고, “인터럽트에서 돌아가기 직전”에 양보하도록 설계했다.

핵심 이유는 두 가지다.

1) **`thread_tick()`은 외부 인터럽트 문맥에서 실행된다.**
   - `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/thread.c`
     - 주석: “각 타이머 틱마다 … 외부 인터럽트 문맥에서 실행된다.”
     - 코드: `if (++thread_ticks >= TIME_SLICE) intr_yield_on_return ();`

2) **`thread_yield()`는 외부 인터럽트 문맥에서 호출되면 안 된다.**
   - `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/thread.c`
     - `thread_yield()`는 `ASSERT (!intr_context ());`를 가진다.

그래서 PintOS는 다음처럼 “안전한 지점”에서만 `thread_yield()`가 실행되게 만든다.

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/interrupt.c`
  - `intr_yield_on_return()`는 `yield_on_return = true`만 해 둔다.
  - `intr_handler()`의 외부 인터럽트 마무리 구간에서
    - `in_external_intr = false;`로 인터럽트 문맥 표시를 내린 뒤
    - `pic_end_of_interrupt(...)`로 EOI를 보내고
    - `if (yield_on_return) thread_yield();`를 호출한다.

이 말은 즉:

- 인터럽트가 끝나는 “출구”에서 문맥 표시를 정리한 다음에야 스케줄링을 허용한다.

현실 OS(Linux/Windows)도 큰 방향은 비슷하다.

- “하드웨어 인터럽트 핸들러 한가운데서” 블로킹/스케줄링을 하는 건 위험하므로,
- 보통은 “return from interrupt” 근처에서 reschedule을 하거나, deferred work로 넘기는 구조를 쓴다.

## QEMU에서는

PintOS가 받는 timer interrupt는 QEMU의 가상 장치가 guest CPU에 전달하는 이벤트다.

QEMU는 host process 안에서 virtual timer/device state를 관리하고, guest가 보기에는 hardware interrupt가 온 것처럼 만든다.

확인할 QEMU 영역:

- `/Users/woonyong/workspace/Krafton-Jungle/QEMU/hw/timer/`
- `/Users/woonyong/workspace/Krafton-Jungle/QEMU/hw/intc/`
- `/Users/woonyong/workspace/Krafton-Jungle/QEMU/hw/i386/`
- `/Users/woonyong/workspace/Krafton-Jungle/QEMU/target/i386/`

## 차이점

| 항목 | Linux / Windows | PintOS | QEMU |
|---|---|---|---|
| interrupt source | 실제 장치와 APIC | 교육용 설정 위의 interrupt handler | 가상 장치가 guest interrupt 생성 |
| scheduling | 다중 CPU, 복잡한 정책 | ready list 중심 단순 정책 | scheduling하지 않음. 하드웨어 이벤트를 흉내 냄 |
| debugging | kernel tracing 필요 | GDB로 비교적 직접 추적 가능 | guest와 host QEMU 코드를 구분해야 함 |

## 코드 증거

PintOS에서 타이머 틱이 스케줄링으로 이어지는 핵심은 `thread_tick`이다.

파일:

- `/Users/woonyong/workspace/Krafton-Jungle/SW_AI-W09-pintos/pintos/threads/thread.c`

핵심 코드:

```c
if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
```

이 코드는 현재 스레드가 정해진 time slice를 다 쓰면, 인터럽트에서 복귀할 때 양보하도록 예약한다는 뜻이다.

실제 스케줄러는 다음 스레드를 고른 뒤 새 time slice를 시작한다.

```text
schedule()
  -> next_thread_to_run()
  -> next->status = THREAD_RUNNING
  -> thread_ticks = 0
```

QEMU 쪽에서 interrupt가 guest CPU로 전달되는 핵심 단서는 APIC 코드의 interrupt 요청이다.

파일:

- `/Users/woonyong/workspace/Krafton-Jungle/QEMU/hw/intc/apic.c`

핵심 코드:

```c
if (apic_irq_pending(s) > 0) {
    cpu_interrupt(cpu, CPU_INTERRUPT_HARD);
}
```

이 코드는 QEMU가 guest CPU에 "하드웨어 인터럽트가 pending 상태"라고 알릴 수 있음을 보여준다.

## 숫자 예제

```text
TIME_SLICE = 4

tick 1: current thread runs
tick 2: current thread runs
tick 3: current thread runs
tick 4: time slice expires
        yield on interrupt return
```

## 메모리 보기

interrupt 진입 시 CPU 상태는 stack 또는 interrupt frame에 저장된다. 이 저장된 바이트들을 struct로 보면 register snapshot이 된다.

## 디버깅 체크리스트

- `timer_interrupt`에 breakpoint
- `thread_tick`에서 현재 thread와 tick 값 확인
- interrupt return 직전 yield flag 확인
- QEMU 쪽에서는 timer device와 interrupt injection 경로를 별도 추적

## 다음으로 볼 문서

- [[week-1-threads-map]]
- [[cpu-register-execution]]
