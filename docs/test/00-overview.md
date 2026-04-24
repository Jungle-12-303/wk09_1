# Project 1: Threads — 테스트 총정리

## 배점 구조

| Phase | 카테고리 | 테스트 수 | 배점 비중 |
|-------|---------|----------|----------|
| Phase 1 | Alarm Clock | 6개 | 20% |
| Phase 2 | Priority Scheduling | 5개 | 50% (일부) |
| Phase 3 | Priority Donation | 7개 | 50% (일부) |
| Phase 4 | MLFQS | 9개 | 30% |

**총 27개 테스트.**

## 테스트 실행 방법

```bash
# 도커 컨테이너 접속 후
source /IdeaProjects/SW_AI-W09-pintos/pintos/activate

# 빌드
cd pintos/threads && make

# 전체 테스트
make check

# 개별 테스트
make tests/threads/alarm-multiple.result

# MLFQS 테스트 (별도 플래그)
pintos -- -q -mlfqs run mlfqs-load-1
```

## 수정 대상 파일 요약

| 파일 | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|------|---------|---------|---------|---------|
| `devices/timer.c` | **핵심** | - | - | - |
| `threads/thread.h` | 구조체 추가 | 구조체 추가 | 구조체 추가 | 구조체 추가 |
| `threads/thread.c` | - | **핵심** | **핵심** | **핵심** |
| `threads/synch.c` | - | **핵심** | **핵심** | - |

## Phase별 상세 문서

- [Phase 1: Alarm Clock](./01-alarm-clock.md) — busy wait 제거
- [Phase 2: Priority Scheduling](./02-priority-scheduling.md) — 우선순위 기반 스케줄링
- [Phase 3: Priority Donation](./03-priority-donation.md) — 우선순위 기부 (역전 방지)
- [Phase 4: MLFQS](./04-mlfqs.md) — 다단계 피드백 큐 스케줄러

## 권장 구현 순서

1. **Phase 1 (Alarm Clock)** → 가장 독립적, 나머지의 기반
2. **Phase 2 (Priority Scheduling)** → Phase 3의 전제 조건
3. **Phase 3 (Priority Donation)** → Phase 2 위에 구축
4. **Phase 4 (MLFQS)** → 독립적이지만 가장 복잡, `thread_mlfqs` 플래그로 분기
