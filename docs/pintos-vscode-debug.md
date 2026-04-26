# VS Code Pintos F5 디버깅

이 저장소는 Dev Container 안에서 VS Code F5로 Pintos 커널을 GDB 디버깅할 수 있게 설정되어 있다.

## 처음 준비

1. VS Code에서 저장소 루트 폴더를 연다.
2. `Dev Containers: Reopen in Container`를 실행한다.
3. 이미 컨테이너가 열려 있었고 설정을 새로 받은 상태라면 `Dev Containers: Rebuild Container`를 실행한다.

컨테이너 안에서는 `pintos/utils`가 `PATH`에 들어가므로 `pintos` 명령을 바로 사용할 수 있다.

## F5 실행

1. Run and Debug 탭을 연다.
2. 디버그 구성을 고른다.
   - `Pintos: threads`
   - `Pintos: userprog`
   - `Pintos: vm`
3. F5를 누른다.
4. 테스트 이름을 입력한다.

예시는 다음과 같다.

```text
alarm-zero
alarm-negative
args-none
```

파일명인 `alarm-zero.c`를 입력하면 안 된다. 테스트 이름은 `.c`를 뺀 이름이다.

## 내부 동작

F5를 누르면 VS Code가 다음 순서로 실행한다.

```text
make
scripts/pintos-gdb-server.sh <project> <test-name>
pintos --gdb -- -q run <test-name>
GDB attach localhost:1234
```

정상 로그에는 다음처럼 보여야 한다.

```text
Kernel command line: -q run alarm-zero
```

아래처럼 보이면 VS Code task나 script 인자 구성이 잘못된 것이다.

```text
Kernel command line: -- -q run alarm-zero
```

## 추천 브레이크포인트

처음 확인할 때는 테스트 파일보다 커널 코드에 먼저 브레이크포인트를 거는 편이 안정적이다.

- `pintos/devices/timer.c`의 `timer_sleep`
- `pintos/threads/thread.c`의 `thread_unblock`
- `pintos/threads/thread.c`의 `schedule`
- `pintos/threads/init.c`의 `main`

`alarm-negative.c` 같은 테스트 파일은 단독 실행 파일이 아니다. Pintos 커널이 부팅된 뒤 `run alarm-negative` 명령을 처리하면서 테스트 함수를 호출할 때 멈춘다.

## 브레이크포인트가 안 먹을 때

- Dev Container 안에서 실행 중인지 확인한다.
- Run and Debug에서 맞는 구성을 골랐는지 확인한다.
- 테스트 이름에 `.c`를 붙이지 않았는지 확인한다.
- `threads` 테스트를 보면서 `userprog` 또는 `vm` 구성을 고르지 않았는지 확인한다.
- 설정을 바꾼 뒤 `Developer: Reload Window`를 실행한다.
- 그래도 이상하면 `Dev Containers: Rebuild Container`를 실행한다.
- `pintos/<project>/build/kernel.o`가 최근 코드로 빌드되었는지 확인한다.

## 수동 실행

VS Code 밖에서 확인하고 싶으면 프로젝트 build 디렉토리에서 실행한다.

```bash
cd pintos/threads/build
pintos --gdb -- -q run alarm-negative
```

다른 터미널에서는 다음처럼 붙을 수 있다.

```bash
cd pintos/threads/build
gdb kernel.o
(gdb) target remote localhost:1234
```
