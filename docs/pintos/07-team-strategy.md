# 팀 협업 전략: 각자 구현 + 돌아가며 머지

## 전제 조건

- 4인 팀, 실력 차이가 있음
- 팀원 모두 Pintos가 처음이라 "이끄는 사람"이 없음
- Git 경험이 적은 팀원이 있음
- 기간: 1주

## 핵심 방침

**각자 전부 구현하고, 머지 담당은 돌아가면서 맡는다.**

한 사람의 코드만 master에 올라가는 방식이 아니다.
머지 담당자가 4명의 코드를 전부 읽고, 비교하고, 직접 합치거나 기준 코드를 선택한다.
이 과정에서 머지 담당자는 자연스럽게 코드 리뷰와 충돌 해결을 경험하게 된다.

---

## 왜 이 방식인가

### "분업 후 마지막에 합치기"의 문제

Alarm Clock을 A가, Priority Scheduling을 B가, MLFQS를 C가 맡으면 어떻게 될까.
Priority Scheduling은 Alarm Clock의 sleep_list 구조 위에서 동작하고,
MLFQS는 Priority Scheduling의 thread_set_priority 구조 위에서 동작한다.
서로의 코드를 모르는 상태에서 합치면 컴파일조차 안 될 가능성이 높다.
게다가 자기가 맡지 않은 부분은 이해하지 못한 채 넘어가게 된다.

### "잘하는 사람 코드만 머지"의 문제

한 명이 계속 master에 올리면 나머지 3명은 그 코드를 복붙하는 것과 다를 바 없다.
발표 때 "이 코드 설명해보세요"라는 질문에 답하지 못하게 된다.
무엇보다 OS 과제의 목적 자체가 직접 구현해보며 배우는 것이다.

### "각자 구현 + 돌아가며 머지"의 장점

전원이 전체 과제를 직접 경험한다.
머지 담당자는 4명의 코드를 비교하면서 "같은 문제에 대한 다른 접근"을 보게 된다.
Git 충돌 해결, 브랜치 관리, PR 리뷰를 실전으로 익힌다.
누구 하나의 코드에 의존하지 않으므로 팀원 모두 설명할 수 있는 코드가 된다.

---

## 일정 및 역할 배분

### 전체 타임라인

```
Day 1       : Alarm Clock        — 머지 담당 A
Day 2-3     : Priority Scheduling — 머지 담당 B
Day 4-5     : Priority Donation   — 머지 담당 C
Day 6-7     : MLFQS              — 머지 담당 D
```

각 Phase가 끝나는 시점의 코어타임에 머지를 진행한다.

### 하루 흐름

```
오전     : 각자 개인 브랜치에서 구현
코어타임 : 진행 상황 공유 (각자 화면 보여주며 설명)
          머지 담당자가 기준 코드 선택 및 머지 실행
오후/저녁: 머지된 master 기반으로 다음 Phase 시작
```

---

## 브랜치 전략

### 구조

```
master                          (항상 빌드 가능한 상태)
  |
  +-- member/woonyong          (우녕 개인 브랜치)
  +-- member/teammate-b        (팀원 B 개인 브랜치)
  +-- member/teammate-c        (팀원 C 개인 브랜치)
  +-- member/teammate-d        (팀원 D 개인 브랜치)
```

### 개인 브랜치 생성

```bash
# 최초 1회
git checkout master
git checkout -b member/woonyong

# 새 Phase 시작 시 master 최신화 후 작업
git checkout master
git pull origin master
git checkout member/woonyong
git merge master
```

### 개인 작업 push

```bash
git add <수정한 파일>
git commit -m "feat: Alarm Clock sleep_list 방식으로 구현"
git push origin member/woonyong
```

---

## 머지 담당자 가이드

머지 담당은 해당 Phase에서 가장 중요한 역할이다.
아래 순서를 따른다.

### 1단계: 4명의 코드 확인

코어타임에 각자 자기 코드를 화면으로 보여주며 설명한다.
머지 담당자는 아래 기준으로 각 코드를 평가한다.

```
- 테스트가 통과하는가? (make check 결과)
- 코드가 읽기 쉬운가?
- 불필요한 코드가 없는가?
```

완벽한 코드를 찾으려 하지 않는다.
"돌아가는 코드" 중에서 가장 깔끔한 것을 기준으로 고른다.

### 2단계: 기준 코드 선택 및 머지

```bash
# master를 최신 상태로
git checkout master
git pull origin master

# 기준 코드를 머지
git merge origin/member/teammate-b
```

충돌이 없으면 그대로 진행한다.
충돌이 발생하면 3단계로 간다.

### 3단계: 충돌 해결

```bash
# 충돌 파일 확인
git status

# 충돌 파일을 열면 아래와 같은 마커가 보인다
<<<<<<< HEAD
    현재 master의 코드
=======
    머지하려는 브랜치의 코드
>>>>>>> origin/member/teammate-b

# 두 코드를 비교해서 올바른 쪽을 남기고 마커를 삭제한다
# 판단이 어려우면 팀원에게 물어본다

# 해결 후
git add <충돌 해결한 파일>
git commit -m "merge: teammate-b의 Alarm Clock 구현을 master에 병합"
```

### 4단계: 검증 및 push

```bash
# 빌드 확인
cd pintos/threads
make clean && make

# 테스트 실행
cd build
make check

# 통과하면 push
git checkout master
git push origin master
```

### 5단계: 전원 master 동기화

머지가 끝나면 나머지 팀원이 각자 브랜치를 업데이트한다.

```bash
git checkout master
git pull origin master
git checkout member/woonyong
git merge master
```

---

## 머지 담당자가 판단이 안 될 때

### "4명 코드가 다 다른데 뭘 골라야 하지?"

테스트 통과 여부로 1차 필터링한다.
테스트를 통과하는 코드가 여러 개면 가장 짧고 단순한 코드를 고른다.
아무도 테스트를 통과하지 못했으면, 가장 많이 통과한 코드를 기준으로
나머지 사람들과 함께 디버깅한다.

### "다른 사람 코드에서 좋은 부분만 가져오고 싶다"

가능하다. 기준 코드를 머지한 뒤, 다른 사람의 특정 함수만 수동으로 가져오면 된다.
예를 들어 A의 전체 구조를 기준으로 하되, B의 compare 함수가 더 깔끔하면
B의 해당 함수만 복사해서 넣는다.

### "충돌이 너무 많아서 못 합치겠다"

기준 코드 하나만 머지하고, 나머지는 머지하지 않는다.
이것은 실패가 아니다. 머지 담당자가 "이 코드가 가장 적합하다"고 판단한 것이다.
대신 선택하지 않은 코드의 좋은 아이디어는 구두로 공유한다.

---

## Phase별 머지 체크리스트

### Phase 1: Alarm Clock (머지 담당 A)

```
확인 사항:
[ ] struct thread에 wake_tick 필드가 추가되었는가
[ ] sleep_list가 선언되고 초기화되었는가
[ ] timer_sleep()에서 busy-wait가 제거되었는가
[ ] timer_interrupt()에서 깨우기 로직이 동작하는가
[ ] alarm-single, alarm-multiple 테스트 통과

머지 대상 파일:
- include/threads/thread.h
- devices/timer.c
- threads/thread.c (선택)
```

### Phase 2: Priority Scheduling (머지 담당 B)

```
확인 사항:
[ ] ready_list가 우선순위 순으로 정렬되는가
[ ] thread_create() 후 선점이 발생하는가
[ ] thread_set_priority() 후 선점이 발생하는가
[ ] sema_up()에서 가장 높은 우선순위가 깨어나는가
[ ] cond_signal()에서 우선순위 순으로 signal 되는가
[ ] priority-change, priority-preempt, priority-fifo 테스트 통과

머지 대상 파일:
- threads/thread.c
- threads/synch.c
```

### Phase 3: Priority Donation (머지 담당 C)

```
확인 사항:
[ ] struct thread에 original_priority, wait_on_lock, donations 필드 추가
[ ] lock_acquire()에서 기부가 발생하는가
[ ] lock_release()에서 기부가 회수되는가
[ ] 중첩 기부(nested donation)가 동작하는가
[ ] 다중 기부(multiple donation)가 동작하는가
[ ] priority-donate-one, priority-donate-nest, priority-donate-chain 테스트 통과

머지 대상 파일:
- include/threads/thread.h
- threads/thread.c
- threads/synch.c
```

### Phase 4: MLFQS (머지 담당 D)

```
확인 사항:
[ ] fixed_point.h 매크로가 정확한가
[ ] struct thread에 nice, recent_cpu 필드 추가
[ ] load_avg 전역 변수가 올바르게 초기화되는가
[ ] timer_interrupt()에서 매 틱/4틱/초 단위 업데이트가 동작하는가
[ ] thread_set_priority()가 MLFQS 모드에서 무시되는가
[ ] mlfqs-load-1, mlfqs-recent-1 테스트 통과

머지 대상 파일:
- include/threads/thread.h (또는 threads/fixed_point.h 신규)
- threads/thread.c
- devices/timer.c
```

---

## Git 기초 명령어 요약

팀원 전원이 알아야 할 최소한의 명령어 목록이다.

```bash
# 상태 확인
git status                    # 현재 변경 사항 확인
git log --oneline -10         # 최근 커밋 10개 확인
git branch -a                 # 모든 브랜치 확인

# 일상 작업
git add <파일>                # 스테이징
git commit -m "메시지"         # 커밋
git push origin <브랜치>       # 원격에 push
git pull origin master        # master 최신화

# 브랜치 이동
git checkout master           # master로 이동
git checkout member/woonyong  # 개인 브랜치로 이동

# 머지
git merge <브랜치>             # 현재 브랜치에 다른 브랜치 합치기

# 실수 복구
git stash                     # 작업 중인 변경사항 임시 저장
git stash pop                 # 임시 저장한 것 복원
git log --oneline             # 어디까지 커밋했는지 확인
```

### 절대 하지 말 것

```
git push --force              # 원격 히스토리 강제 덮어쓰기
git reset --hard              # 로컬 변경 전부 삭제
master 브랜치에서 직접 코딩     # 반드시 개인 브랜치에서 작업
```

---

## 코어타임 진행 순서 (매일)

```
1. 전원 git push (각자 개인 브랜치에 최신 코드 올리기)      — 5분
2. 돌아가며 화면 공유로 자기 코드 설명                      — 각 5분, 총 20분
   - "나는 이렇게 접근했고, 여기서 막혔다"
   - 완성 못 했어도 괜찮다. 어디까지 했는지가 중요
3. 머지 담당자가 기준 코드 선택 선언                        — 5분
   - "테스트 가장 많이 통과한 C의 코드를 기준으로 합치겠다"
4. 머지 실행 (전원이 화면 보면서 같이 진행)                  — 20분
   - 충돌 나면 같이 논의해서 해결
5. master에 push 후 전원 동기화                            — 10분
```

총 60분 정도 소요된다.

---

## 발표 준비

목요일 발표 자료에 포함할 내용은 다음과 같다.

### 프로젝트 팀 구성

각 팀원의 역할과 머지 담당 배분을 설명한다.

### 프로젝트 구현 및 트러블슈팅

각자 1인당 2분 발표이므로, 자기가 머지 담당했던 Phase를 중심으로
"어떤 기준으로 코드를 선택했는지, 충돌이 어디서 났는지, 어떻게 해결했는지"를
설명하면 된다. 이것이 가장 설득력 있는 트러블슈팅 사례가 된다.

### 프로젝트 회고

"각자 구현 + 돌아가며 머지" 방식이 잘 동작한 점과 개선할 점을 정리한다.
