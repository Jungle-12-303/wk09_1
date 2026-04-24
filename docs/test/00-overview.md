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

프로젝트 루트(`SW_AI-W09-pintos/`)에서 실행한다.

```bash
# 빌드
make

# 빌드 + 테스트 실행 (한방에)
make br T=alarm-multiple

# 실행만 (빌드 없이)
make run T=alarm-multiple

# 개별 테스트 채점 (기대 출력과 비교)
make result T=alarm-multiple

# 전체 테스트
make check

# MLFQS 테스트 (별도 플래그)
make mlfqs T=mlfqs-load-1

# 테스트 목록 보기
make run
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
