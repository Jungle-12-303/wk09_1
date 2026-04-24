# Q02. thread_init 함수의 동작 원리 -- GDT, desc_ptr, 스레드 등록

> Pintos threads/thread.c | 기본

## 질문

1. thread_init()이 하는 세 가지 작업은 각각 무엇이고 왜 필요한가
2. desc_ptr 구조체는 무엇이고 어디에 쓰이는가
3. "현재 실행 흐름을 main 스레드로 등록한다"는 것이 구체적으로 무슨 뜻인가

## 답변

### 최우녕

> thread_init()이 하는 세 가지 작업은 각각 무엇이고 왜 필요한가

부팅 직후 CPU는 코드를 순차 실행하고 있을 뿐, Pintos의 스레드 시스템은 아직 존재하지 않는다.
struct thread도 없고, ready_list도 없고, thread_current()를 호출하면 터진다.
thread_init()은 "스레드 개념이 없는 상태"에서 "스레드 시스템이 작동하는 상태"로 전환한다.

**1) 임시 GDT 로드**

GDT(Global Descriptor Table)는 CPU가 메모리 세그먼트를 관리하는 테이블이다.
커널 모드에서 동작하기 위한 최소 설정으로, 나중에 gdt_init()에서 유저 세그먼트를 포함한 완전한 GDT로 교체된다.

```c
struct desc_ptr gdt_ds = {
    .size = sizeof(gdt) - 1,
    .address = (uint64_t) gdt
};
lgdt(&gdt_ds);  // CPU에게 "GDT가 여기 있어" 알려줌
```

**2) 전역 자료구조 초기화**

```c
lock_init(&tid_lock);        // tid 발급 시 동시 접근 방지용 락
list_init(&ready_list);      // 실행 대기 큐 (스케줄러가 여기서 다음 스레드를 꺼냄)
list_init(&destruction_req); // 종료된 스레드 메모리 해제 대기열
```

**3) 현재 실행 흐름을 "main" 스레드로 등록**

지금 CPU가 실행 중인 main() 함수 흐름에 이름표를 붙여서 정식 스레드로 만든다.

> desc_ptr 구조체는 무엇이고 어디에 쓰이는가

x86-64 CPU에 GDT나 IDT의 위치를 알려줄 때 쓰는 구조체다.
CPU의 lgdt/lidt 명령어가 요구하는 포맷이 이것이다.

```c
// include/threads/mmu.h
struct desc_ptr {
    uint16_t size;      // 테이블의 바이트 크기 - 1
    uint64_t address;   // 테이블의 메모리 주소
} __attribute__((packed));  // 패딩 없이 배치 (CPU가 정확한 바이트 레이아웃을 기대)
```

비유하면 사전(GDT)은 책장(RAM)에 이미 꽂혀 있는데, 사람(CPU)에게 "사전은 3번 선반에 있어"라고 알려주는 봉투 역할이다.
사전을 만들거나 올리는 게 아니라 위치를 가리켜주는 것이다.

사용되는 곳:
- thread_init()에서 lgdt()로 GDT 등록
- interrupt.c에서 lidt()로 IDT 등록
- gdt.c에서 완전한 GDT로 교체 시

> "현재 실행 흐름을 main 스레드로 등록한다"는 것이 구체적으로 무슨 뜻인가

부팅 후 main()이 이미 실행 중이지만, 아직 struct thread가 없어서 "스레드"가 아니다.
이 실행 흐름에 신분증을 발급하는 과정이다.

```c
initial_thread = running_thread();
// 현재 스택 포인터(RSP)를 4KB 경계로 내림해서 struct thread 위치를 역산

init_thread(initial_thread, "main", PRI_DEFAULT);
// 이름="main", 우선순위=31(PRI_DEFAULT)로 초기화

initial_thread->status = THREAD_RUNNING;
// 상태를 "실행 중"으로 표시

initial_thread->tid = allocate_tid();
// tid = 1 부여
```

Pintos에서 각 스레드는 4KB 페이지를 하나 차지하고, 그 페이지 맨 아래에 struct thread가 있다.
running_thread()는 현재 RSP를 4KB 경계로 내림하면 struct thread 시작 주소가 나오는 원리를 이용한다.

### 수치 검증

struct thread의 4KB 페이지 레이아웃:

```
           4 KB (0x1000)
┌───────────────────────┐ ← 페이지 끝 (높은 주소)
│                       │
│     커널 스택          │   스택은 위에서 아래로 자란다
│     (약 3.5KB)        │   ← RSP가 여기 어딘가를 가리킴
│                       │
│         ↓             │
├───────────────────────┤
│   struct thread       │   약 0.5KB
│   (이름, 우선순위,    │
│    상태, tid, magic)  │
└───────────────────────┘ ← 페이지 시작 (낮은 주소)
```

RSP가 0x1800이라면:
- 4KB(0x1000) 경계로 내림 → 0x1000
- 0x1000이 struct thread 시작 주소
- running_thread()가 반환하는 값 = 0x1000

이 함수 호출 전에는 thread_current()를 쓸 수 없다.
이 함수 이후 + palloc_init() 이후에야 thread_create()가 가능하다.

## 연결 키워드

- q01-dev-env-gdb-debug.md -- 디버깅 환경에서 thread_init 중단점 활용
- q03-boot-process-loader.md -- thread_init 이전의 부팅 과정
- docs/test/02-priority-scheduling.md -- ready_list 기반 스케줄링
