# 11. PintOS GDB 디버깅 환경 가이드 — CLion / VSCode

> Docker Dev Container 환경에서 PintOS Project (KAIST/Krafton Jungle) 를 GDB 로 원격 디버깅하기 위한 종합 안내서. CLion 과 VSCode 두 IDE 에 대해 처음 세팅부터 실전 디버깅 패턴까지 단계별로 다룬다.

---

## 0. 이 문서의 사용법

- **처음 세팅하는 사람** → §1 (공통) → §2 (CLion) 또는 §3 (VSCode) → §4 (실전)
- **특정 문제만 검색** → §6 트러블슈팅
- **단축키만 빠르게** → 부록 A
- **CLion vs VSCode 비교** → §5

용어:

- **호스트** — Mac (CLion / VSCode 가 도는 곳)
- **컨테이너** — Docker Dev Container (PintOS 빌드·실행이 도는 곳)
- **gdbstub** — QEMU 가 제공하는 GDB 원격 프로토콜 서버 (포트 1234)

---

## 1. 사전 조건 및 공통 준비

### 1.1 환경

항목요구 사항OSmacOS (Apple Silicon 또는 Intel)DockerDocker Desktop 설치·실행 중Dev ContainerKAIST/Jungle 제공 PintOS 이미지 사용IDECLion 2024.1+ 또는 VSCode 최신

### 1.2 경로 매핑 — 머릿속에 새겨두기

호스트와 컨테이너의 동일한 프로젝트가 다른 경로로 보인다.

호스트 (Mac)컨테이너`/Users/<user>/workspace/Krafton-Jungle/SW_AI-W09-pintos/IdeaProjects/SW_AI-W09-pintos~/Library/Application Support/JetBrains/CLion2026.1/`(Mac 전용)(없음)`~/.gdbinit` (= `/home/jungle/.gdbinit`)

**원칙**: IDE 가 컨테이너 안에서 돌면 (CLion Dev Container, VSCode Remote-Container) **컨테이너 경로** 를 쓴다. 호스트에서 직접 띄우면 호스트 경로를 쓴다.

### 1.3 PintOS 빌드

컨테이너 셸에서:

```bash
cd /IdeaProjects/SW_AI-W09-pintos/pintos/threads
make
```

빌드 산출물 (모두 `pintos/threads/build/` 안):

파일용도`kernel.o`디버그 심볼 포함 ELF — 디버거의 Symbol file`kernel.bin`스트립된 부팅 바이너리`os.dsk`QEMU 가 부팅할 디스크 이미지`loader.bin`부트로더

`kernel.o` 가 핵심이다. 디버거에 이 파일 경로를 알려줘야 소스 줄과 어셈블리가 매핑된다.

### 1.4 루트 편의 Makefile (권장)

매번 `cd pintos/threads/build && pintos --gdb -- ...` 치기 귀찮다. 루트에 다음 Makefile 을 두면 `make gdb T=alarm-multiple` 한 줄로 끝.

```makefile
SHELL := /bin/bash

THREADS_DIR = pintos/threads
BUILD_DIR   = $(THREADS_DIR)/build
ACTIVATE    = source pintos/activate &&

build:
	cd $(THREADS_DIR) && make

run:
ifndef T
	@echo "Usage: make run T=<test-name>"
else
	$(ACTIVATE) cd $(BUILD_DIR) && pintos -- -q run $(T)
endif

result:
ifndef T
	@echo "Usage: make result T=<test-name>"
else
	$(ACTIVATE) cd $(BUILD_DIR) && make tests/threads/$(T).result
endif

gdb:
ifndef T
	@echo "Usage: make gdb T=<test-name>"
else
	$(ACTIVATE) cd $(BUILD_DIR) && pintos --gdb -- -q run $(T)
endif

check:
	$(ACTIVATE) cd $(THREADS_DIR) && make check

clean:
	cd $(THREADS_DIR) && make clean
```

**핵심 포인트**:

- `SHELL := /bin/bash` — `/bin/sh` (dash) 는 `source` 를 모름. 필수.
- `ACTIVATE = source pintos/activate &&` — `pintos` 명령을 PATH 에 추가하는 스크립트를 매 타겟마다 미리 source.
- 모든 pintos 호출 타겟 앞에 `$(ACTIVATE)` 를 prepend.

### 1.5 QEMU 를 GDB 대기 모드로 기동

별도 터미널 (CLion 내장 또는 외부) 에서:

```bash
make gdb T=alarm-multiple
```

내부적으로 다음이 실행됨:

```bash
source pintos/activate && \
cd pintos/threads/build && \
pintos --gdb -- -q run alarm-multiple
```

`--gdb` 옵션이 핵심. QEMU 가 1234 포트 `tcp::1234,server,nowait` 로 GDB 연결을 기다리며 첫 명령어에서 멈춘다. 터미널은 **출력 없이 멈춘 상태가 정상**. 이 창은 디버그 끝날 때까지 닫지 말 것.

확인:

```bash
# 다른 터미널에서
ss -lntp | grep 1234
# LISTEN 0 1 *:1234 ... 같은 게 나오면 OK
```

### 1.6 캐시 주의 — make result 가 "up to date" 일 때

테스트 결과 파일이 이미 있으면 make 가 재실행 안 함:

```bash
$ make result T=alarm-multiple
make[1]: 'tests/threads/alarm-multiple.result' is up to date.
```

강제 재실행:

```bash
rm -f pintos/threads/build/tests/threads/alarm-multiple.{result,output}
make result T=alarm-multiple
```

또는 코드 수정 후엔 `make` (재빌드) 부터:

```bash
make build && make result T=alarm-multiple
```

---

## 2. CLion 가이드

### 2.1 Dev Container 모드 이해

CLion 으로 프로젝트를 Dev Container 로 열면:

```
[Mac]                              [Docker Container]
  CLion UI 클라이언트   ←JB 프로토콜→   CLion 백엔드
                                         ├─ 인덱싱·빌드·디버거 모두 여기서
                                         └─ gdb / make 등 모든 명령 여기서
                                         
                                         QEMU (별도 터미널에서 기동)
                                         └─ :1234 (같은 컨테이너 loopback)
```

**중요한 결과**:

- CLion 백엔드가 컨테이너 안에 있으므로 `localhost:1234` 는 **컨테이너 loopback**.
- 호스트 (Mac) 에서 1234 포트 노출 (포트 포워딩) 안 해도 된다.
- Symbol file 경로는 **컨테이너 경로** (`/IdeaProjects/...`) 사용.
- Path mappings 도 비워둬도 된다.

확인 방법: 호스트 터미널에서 `docker inspect <container_id> --format '{{json .Mounts}}'` 결과에 `jb_devcontainers_shared_volume` 이 있으면 JetBrains Dev Container 모드.

### 2.2 GDB Remote Debug 구성 만들기

`Run → Edit Configurations... → + (왼쪽 위)`

목록에서 **"원격 디버그" (영문 "GDB Remote Debug")** 선택.

⚠️ "원격 GDB 서버" (Remote GDB Server) 와 다름 — 이건 CLion 이 직접 gdbserver 를 띄우는 방식이라 우리 케이스에 안 맞음.

### 2.3 구성 필드 입력

필드 (한글)영문값이름Name`Pintos GDB`디버거Debugger`번들로 포함된 GDB multiarch` (그대로)'target remote' 인수'target remote' args`tcp:localhost:1234`심볼 파일Symbol file`/IdeaProjects/SW_AI-W09-pintos/pintos/threads/build/kernel.o`SysrootSysroot(비움)경로 매핑Path mappings(비움)실행 전Before launch(비움)

⚠️ **실행 전 (Before launch) 에 빌드 작업 추가하지 말 것**. PintOS 는 Makefile 기반인데 CLion 이 자동 만든 CMake 더미 (pintos_dummy) 를 빌드하려다 실패함.

### 2.4 디버그 시작 — 표준 워크플로우

**1. QEMU 기동** (컨테이너 터미널):

```bash
make gdb T=alarm-multiple
```

대기 상태로 멈춤.

**2. CLion 에서 디버그 클릭**:

오른쪽 위 드롭다운에서 `Pintos GDB` 선택 → 🐞 (벌레 아이콘) 클릭. 또는 단축키 (키맵에 따라 다름).

**3. 자동으로 첫 stop**:

QEMU 가 BIOS 단계 (`0x000000000000fff0`) 에서 멈춰있는 상태로 디버거가 붙는다. 콘솔에:

```
Remote debugging using tcp:localhost:1234
0x000000000000fff0 in ?? ()
The target architecture is set to "auto" (currently "i386:x86-64").
```

**4. 중단점 찍고 Resume**:

원하는 곳에 중단점 찍은 뒤 Resume (F5 / F9 키맵에 따라).

### 2.5 키맵 — Visual Studio 스타일 (선택)

CLion 기본 macOS 키맵은 F8/F7 인데, VSCode·VS 출신이면 F10/F11 이 편하다. 다만 `Visual Studio` **키맵 통째로 적용하면 ⌘+C / ⌘+V / ⌘+S 도 깨진다** (Windows 의 Ctrl+ 조합으로 바뀜). 디버그 키만 갈아끼우는 하이브리드를 권장.

#### 방법: 부분 키맵 파일 만들기

`~/Library/Application Support/JetBrains/CLion2026.1/keymaps/vsDebug.xml`:

```xml
<keymap version="1" name="vsDebug" parent="Mac OS X 10.5+">
  <action id="StepOver">
    <keyboard-shortcut first-keystroke="F8" />
    <keyboard-shortcut first-keystroke="F10" />
  </action>
  <action id="StepInto">
    <keyboard-shortcut first-keystroke="F7" />
    <keyboard-shortcut first-keystroke="F11" />
  </action>
  <action id="StepOut">
    <keyboard-shortcut first-keystroke="shift F8" />
    <keyboard-shortcut first-keystroke="shift F11" />
  </action>
  <action id="Resume">
    <keyboard-shortcut first-keystroke="F5" />
  </action>
  <action id="Stop">
    <keyboard-shortcut first-keystroke="meta F2" />
    <keyboard-shortcut first-keystroke="shift F5" />
  </action>
  <action id="ToggleLineBreakpoint">
    <keyboard-shortcut first-keystroke="meta F8" />
    <keyboard-shortcut first-keystroke="F9" />
  </action>
  <action id="EvaluateExpression">
    <keyboard-shortcut first-keystroke="alt F8" />
    <keyboard-shortcut first-keystroke="shift F9" />
  </action>
</keymap>
```

활성화:

`~/Library/Application Support/JetBrains/CLion2026.1/options/keymap.xml`:

```xml
<application>
  <component name="KeymapManager">
    <active_keymap name="vsDebug" />
  </component>
</application>
```

⚠️ **CLion 을 완전히 종료한 상태에서 파일을 써야 한다**. 켜져 있으면 종료 시 메모리 상태로 덮어씀.

#### Mac 시스템 단축키 충돌 풀기

```
시스템 설정 → 키보드 → 키보드 단축키
  → 기능 키 → "F1, F2 등을 표준 기능 키로 사용" ✓
  → Mission Control → "Show Desktop" (F11) 체크 해제
```

이 두 가지 안 풀면 F11 (Step Into) 이 데스크탑 표시로 가로채진다.

### 2.6 `.gdbinit` 작성 (선택, 영구 자동 명령)

매번 디버그 시작할 때 자동으로 실행할 GDB 명령들. 컨테이너 안에서:

```bash
cat > ~/.gdbinit <<'EOF'
set print pretty on
set print array on
set pagination off
set confirm off
add-auto-load-safe-path /

# 안전한 전역 변수만 자동 표시
display ticks
EOF
```

⚠️ `.gdbinit` **에 위험한 표현식 넣지 말 것**. 다음은 부팅 초반 (BIOS, 첫 stop) 에 평가되면 QEMU 를 크래시시킨다:

```
display thread_current()->name      # ✗ thread_init 전엔 잘못된 메모리
display list_size(&ready_list)      # ✗ ready_list 초기화 전엔 garbage
```

`ticks` 같은 단순 전역 변수만 안전하다.

CLion 2026.1 에서는 `Settings → Build, Execution, Deployment → Debugger → GDB` 의 startup commands 페이지가 사라졌다. `.gdbinit` 가 유일한 영구 startup 방법.

CLion 이 보안상 `.gdbinit` 자동 로드를 막을 수 있다. 위 파일에 이미 `add-auto-load-safe-path /` 가 들어있어서 보통은 자동 처리되지만, 첫 세션에서 무시되면 GDB 콘솔에서 직접 한 번:

```
add-auto-load-safe-path /home/jungle
source ~/.gdbinit
```

### 2.7 디버그 정보 확장 — Watches (강력 추천)

GDB 의 `display` 는 세션 한정인 반면 CLion **Watches** 는:

- 한 번 등록 → 프로젝트 `.idea/` 에 영구 저장
- 모든 디버그 세션에서 자동으로 다시 등장
- 변수 패널 UI 에 깔끔하게 표시
- 구조체면 ▶ 클릭으로 펼쳐볼 수 있음

#### 등록 방법

디버그 세션 중 → 하단 **"스레드 및 변수"** 탭 → 입력칸:

```
표현식(⏎)을 평가하거나 감시(⇧⏎)를 추가하세요
[___________________________________________]
```

**한 줄 입력 + Shift+Enter** (그냥 Enter 는 일회성 평가). 한 줄씩 5 회 반복:

```
ticks
intr_get_level()
thread_current()->name
thread_current()->status
list_size(&ready_list)
```

⚠️ **부팅 후 시점에서 등록**. 첫 stop (BIOS) 에서 등록하면 thread_current() 평가하다 크래시할 수 있다. 안전한 곳 (예: `tests.c:51` 의 for 루프) 에 먼저 도달한 뒤 등록.

#### 일회성 평가 — 즉시 한 번만

같은 입력칸에 표현식 + **그냥 Enter** (Shift 없이) → 결과만 보여주고 사라짐. 위험한 표현식 가끔 확인할 때 안전.

### 2.8 Logpoint — 멈추지 않고 콘솔에 출력

거터의 빨간 원 우클릭 → "더 보기" 또는 더블클릭 → 다이얼로그:

옵션설정**일시 중단 (Suspend)**✗ **체크 해제로그 (Log)**✓ "표현식 평가 후 로깅"표현식C 식, 예: `"sleep req: %s for %lld\n", thread_current()->name, ticks`

이 줄을 지나갈 때 **멈추지 않고** 디버그 콘솔에 메시지만 출력. printf 디버깅을 코드 수정 없이.

PintOS 에 자주 박는 위치:

파일:줄표현식`devices/timer.c` `timer_sleep` 첫 줄`"sleep: %s wants %lld @t=%lld\n", thread_current()->name, ticks, timer_ticks()devices/timer.c` `timer_interrupt"tick %lld -> %s\n", ticks, thread_current()->namethreads/thread.c` `thread_block"block: %s\n", thread_current()->namethreads/thread.c` `thread_unblock"unblock: %s\n", t->name`

### 2.9 조건부 중단점

특정 조건일 때만 멈추기. 중단점 우클릭 → "조건 (Condition)" 에 C 식:

```c
strcmp(thread_current()->name, "thread 3") == 0
ticks > 1000
list_size(&sleep_list) > 5
```

수많은 스레드 중 특정 녀석만 추적할 때 필수.

### 2.10 메모리 뷰 / 레지스터

디버그 창 상단 탭:

- **메모리 뷰** — 주소 또는 표현식 입력 → hex/ASCII dump. 페이지 테이블, 스택 살필 때.
- **GDB** — raw GDB 명령 직접 입력. 부록 C 명령어 모음 참고.

### 2.11 트러블슈팅 — CLion 빈출 문제

**Q. F5 가 동작 안 함**A. (1) 키맵이 `vsDebug` 인지 확인 — 아니면 macOS 기본은 F5 가 비할당. (2) 마우스로 🐞 아이콘 직접 클릭으로 우회.

**Q. 벌레 아이콘 클릭해도 반응 없음**A. (1) 오른쪽 위 드롭다운에 `Pintos GDB` 가 선택돼 있는지. (2) Event Log (View → Tool Windows → Event Log) 에서 에러 확인. (3) QEMU 가 먼저 떠 있는지 (`docker ps` 또는 `ss -lntp | grep 1234`).

**Q. "디버거가 연결 해제되었습니다" 즉시 발생**A. `.gdbinit` 또는 Watches 의 표현식이 부팅 첫 stop 에서 평가되며 QEMU 크래시. 위험한 표현식 (`thread_current()->...`, `list_size`) 제거.

**Q. 중단점 회색 X 표시**A. (1) 심볼 파일 경로 확인 — 컨테이너 경로 (`/IdeaProjects/...`) 인지. (2) 빌드 다시 (`make clean && make`). (3) 다른 프로젝트 (userprog/filesys) 의 동일 파일에 찍었는지 확인.

**Q. 중단점은 활성인데 안 멈춤**A. (1) 그 코드가 실제 호출되는지 확인 (Logpoint 로 추적). (2) `-O0 -g` 빌드인지 확인 — 최적화로 인라인되면 안 멈출 수 있음.

**Q.** `pintos: not found`A. `source pintos/activate` 가 안 됨. Makefile 에 `$(ACTIVATE)` prefix 있는지 확인.

**Q.** `make[1]: 'tests/threads/X.result' is up to date`A. 캐시. `rm -f .../X.{result,output}` 후 재실행. 또는 코드 수정 후엔 자동 재빌드됨.

**Q. 컨테이너 ID 가 매번 바뀜**A. Dev Container 가 매번 새로 생성되는 것. **컨테이너 안에 만든** `~/.gdbinit` **은 같이 사라진다**. 영구화하려면 `/IdeaProjects/.../pintos/threads/build/.gdbinit` (볼륨 마운트 안) 에 두거나 `.devcontainer/devcontainer.json` 의 `postCreateCommand` 에 등록.

---

## 3. VSCode 가이드

### 3.1 필수 확장

확장역할**C/C++** (Microsoft)gdb 연동, IntelliSense**Dev Containers** (Microsoft)컨테이너 안에서 VSCode 실행**CodeLLDB** (선택)LLDB 백엔드 — 우리는 안 씀

### 3.2 Dev Container 로 프로젝트 열기

`Cmd+Shift+P` → **"Dev Containers: Open Folder in Container..."** → 프로젝트 폴더 선택.

`.devcontainer/devcontainer.json` 이 있으면 자동으로 컨테이너 빌드·진입.

VSCode UI 좌하단에 `Dev Container: ...` 라벨이 보이면 컨테이너 모드.

### 3.3 `.vscode/launch.json` 작성

프로젝트 루트에 `.vscode/launch.json` 만들기:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "PintOS GDB",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/pintos/threads/build/kernel.o",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}/pintos/threads/build",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "miDebuggerPath": "/usr/bin/gdb",
      "miDebuggerServerAddress": "localhost:1234",
      "setupCommands": [
        {
          "description": "Enable pretty printing",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        },
        {
          "description": "Pretty print",
          "text": "set print pretty on",
          "ignoreFailures": false
        },
        {
          "description": "Print arrays",
          "text": "set print array on",
          "ignoreFailures": false
        },
        {
          "description": "No paging",
          "text": "set pagination off",
          "ignoreFailures": false
        },
        {
          "description": "Auto-load safe path",
          "text": "add-auto-load-safe-path /",
          "ignoreFailures": true
        },
        {
          "description": "Display ticks (safe global)",
          "text": "display ticks",
          "ignoreFailures": true
        }
      ]
    }
  ]
}
```

⚠️ `setupCommands` **에** `display thread_current()->...` **넣지 말 것**. 부팅 첫 stop 에서 평가되며 크래시한다.

### 3.4 launch.json 필드 설명

필드의미`type: cppdbg`C/C++ 확장의 디버거 타입`request: launch`새로 시작 (attach 와 다름)`program`Symbol file (= `kernel.o`)`cwd`GDB 가 시작될 디렉터리 — `.gdbinit` auto-load 위치 영향`MIMode: gdb`LLDB 가 아닌 GDB 사용`miDebuggerPath`GDB 실행 파일 경로. Jungle 컨테이너는 `/usr/bin/gdb` 또는 `/usr/bin/x86_64-elf-gdbmiDebuggerServerAddress`원격 gdbstub 주소 — QEMU 의 `:1234setupCommands`디버거 시작 직후 실행할 GDB 명령들`stopAtEntry: false`자동으로 첫 명령에서 멈추지 않음 (어차피 QEMU 가 멈춰 있음)

### 3.5 디버그 시작

**1. QEMU 기동** (VSCode 통합 터미널 또는 외부):

```bash
make gdb T=alarm-multiple
```

**2. VSCode 에서 디버그 활성화**:

좌측 사이드바 → 디버그 (▷ 벌레) 아이콘.

**3. 상단 드롭다운에서 "PintOS GDB" 선택 → F5**:

또는 `Run → Start Debugging`.

연결 성공하면 콘솔에:

```
(gdb) target remote localhost:1234
Remote debugging using localhost:1234
0x000000000000fff0 in ?? ()
(gdb) display ticks
1: ticks = 0
```

### 3.6 디버그 정보 확장

#### Watch (왼쪽 사이드바)

`WATCH` 패널 → `+` → 표현식:

```
ticks
intr_get_level()
```

부팅 후 안전한 시점에서만:

```
thread_current()->name
thread_current()->status
list_size(&ready_list)
```

#### Logpoint (Log Message)

거터에서 빨간 원 우클릭 → "Edit Breakpoint" → "Log Message" 선택 → 메시지:

```
sleep req: {thread_current()->name} for {ticks}
```

VSCode 의 Logpoint 표현식은 `{식}` 형태로 변수 보간 (printf 의 `%s` 가 아님).

#### 조건부 중단점

거터 우클릭 → "Add Conditional Breakpoint" → "Expression":

```c
strcmp(thread_current()->name, "thread 3") == 0
```

#### 디버그 콘솔에서 GDB 명령

콘솔 입력칸에 `-exec` prefix 로 GDB 명령:

```
-exec info threads
-exec print/x thread_current()
-exec backtrace
-exec x/16xb 0xc0000000
-exec display thread_current()->name
```

### 3.7 트러블슈팅 — VSCode 빈출 문제

**Q. "Unable to start debugging. Unexpected GDB output...**"A. (1) `miDebuggerPath` 가 정확한지 (`which gdb` 로 확인). (2) gdb 가 x86_64 ELF 를 이해하는지 (`gdb-multiarch` 또는 `x86_64-elf-gdb` 필요할 수 있음).

**Q. "program does not exist**"A. `program` 경로가 잘못. `${workspaceFolder}` 가 컨테이너 경로인지 확인. 의심스러우면 절대경로 (`/IdeaProjects/.../kernel.o`) 직접 입력.

**Q. 즉시 disconnect**A. `setupCommands` 의 표현식 중 `thread_current()` 같은 위험한 거 있는지. 제거.

**Q. 중단점 회색 X**A. (1) 빌드 안 됐거나 심볼 없음. `make clean && make`. (2) source path mapping 필요할 수 있음 — `launch.json` 의 `sourceFileMap` 사용:

```json
"sourceFileMap": {
  "/IdeaProjects/SW_AI-W09-pintos": "${workspaceFolder}"
}
```

**Q. F5 가 다른 동작 함**A. VSCode 키바인딩 충돌. `Cmd+K Cmd+S` (Keyboard Shortcuts) 에서 "Debug: Start Debugging" 확인.

---

## 4. PintOS 전용 디버깅 핵심 — 양쪽 IDE 공통

### 4.1 부팅 단계별 안전 지대

PintOS 부팅 흐름과 각 시점에서 안전한 디버그 표현식:

```
[BIOS / start.S]              ── thread_current() 위험! ──┐
[bss_init]                                                 │
[parse_options]                ← init.c:122 부근            │ 위험 지대
[thread_init]                                              │
   ↓ (이때 struct thread 가 처음 셋업됨)                  ─┘
[console_init, palloc_init, ...]                          ┐
[timer_init]                                              │
[timer_calibrate]               ← init.c:197              │ 안전 지대
[run_actions]                                             │ thread_current() OK
[run_test → t->function()]                                │
[test_alarm_multiple → sleeper 스레드들 → timer_sleep]     ┘
```

**규칙**:

- **첫 stop (BIOS) 에서** `thread_current()` **평가 금지** — 잘못된 메모리 참조로 QEMU 크래시.
- `thread_init();` **이후라면 안전** — 이때부터 `struct thread` 가 유효.
- **운영 코드 (**`run_test` **이후)** 는 모든 표현식 안전.

### 4.2 표현식 안전성 분류표

표현식안전 시점비고`ticks`항상전역 int64_t. 부팅 시 0`intr_get_level()`항상0 = OFF, 1 = ON`thread_current()->name`thread_init 이후char\[16\]`thread_current()->status`thread_init 이후enum thread_status`thread_current()->priority`thread_init 이후int 0\~63`thread_current()->tid`thread_init 이후tid_t`&initial_thread`thread_init 이후첫 스레드 주소`list_size(&ready_list)`thread_init 이후부팅 시 0`list_size(&sleep_list)`sleep_list 정의 이후 (구현 후)Alarm Clock 구현 시

### 4.3 추천 중단점 위치

#### 부팅 흐름 추적용

파일:줄멈추는 시점용도`threads/init.c` `thread_init();` 다음 줄스레드 시스템 초기화 직후thread_current() 첫 안전 지점`threads/init.c` `run_actions(argv);` 호출 줄부팅 완료 직전argv 파싱 결과 확인`tests/threads/tests.c:51` (for 루프)테스트 dispatch테스트 이름 매칭 직전`tests/threads/tests.c:52` (`if (!strcmp...)`)매칭 검사name vs t-&gt;name 비교`tests/threads/tests.c:56` (`t->function ();`)실제 테스트 함수 호출 직전dispatch 마지막 단계

#### Alarm Clock 디버깅용 (가장 중요)

파일:줄멈추는 시점용도`tests/threads/alarm-wait.c` `sleeper` 함수 첫 줄각 sleeper 스레드 진입id, duration 확인`devices/timer.c:93` (`timer_sleep` 첫 줄) ★매 sleep 호출thread_current()-&gt;name 으로 누가 부르는지`devices/timer.c:96` (`while (timer_elapsed...`)busy-wait 루프yield 카운트 추적`devices/timer.c:127` (`timer_interrupt`)매 틱 (1초당 100회)너무 자주 멈추니 조건부 권장

#### Thread 동작 추적용

파일:줄용도`threads/thread.c` `thread_block` 첫 줄누가 언제 블록되나`threads/thread.c` `thread_unblock` 첫 줄누가 누구를 깨우나`threads/thread.c` `schedule`스케줄러 호출`threads/thread.c` `thread_create`스레드 생성 추적

### 4.4 GDB 콘솔 명령어 모음

#### 정보 조회

```
info threads                  # 전체 (gdb 가 인지하는) 스레드
info frame                    # 현재 프레임 상세
info breakpoints (또는 b)     # 중단점 목록
info display                  # 현재 등록된 자동 표시
info registers                # 모든 레지스터
info locals                   # 현재 함수 지역 변수
info args                     # 현재 함수 인자
```

#### 출력

```
print <expr>                  # 평가하고 한 번 출력
print/x <expr>                # 16 진수
print/d <expr>                # 10 진수
print/c <expr>                # 문자
print *<ptr>                  # 포인터 dereference
x/16xb 0xc0000000             # 메모리 16 바이트 hex
x/4wx 0xffffffff80000000      # 4 워드 hex
backtrace (또는 bt)            # 호출 스택
bt full                       # 호출 스택 + 각 프레임의 지역 변수
```

#### 자동 표시 관리

```
display <expr>                # 매번 멈출 때 자동 출력
display/x <expr>              # 16 진수로 자동 출력
info display                  # 현재 활성 display 목록
disable display 2             # 2 번 비활성 (삭제는 아님)
enable display 2              # 다시 활성
delete display 2              # 2 번 삭제
delete display                # 전부 삭제
```

#### 실행 제어

```
continue (c)                  # 다음 중단점까지 재개
step (s)                      # Step Into (소스 한 줄)
next (n)                      # Step Over (소스 한 줄, 함수 진입 X)
finish                        # 현재 함수에서 return 까지
stepi (si)                    # Step Into (어셈블리 한 명령)
nexti (ni)                    # Step Over (어셈블리 한 명령)
```

#### 중단점 / 매크로

```
break <function>              # 함수 진입점에 중단점
break <file>:<line>           # 파일:줄에 중단점
break <file>:<line> if <cond> # 조건부 중단점
delete <num>                  # 중단점 삭제
disable <num>                 # 일시 비활성
condition <num> <expr>        # 기존 중단점에 조건 추가
```

#### PintOS 특화 패턴

```
# 현재 스레드 정보
print thread_current()->name
print thread_current()->status
print thread_current()->priority

# 스레드 구조체 전체 덤프
print *thread_current()

# 특정 주소의 스레드 살피기
print *((struct thread *) 0xffffffff80012000)

# ready 큐 크기
print list_size(&ready_list)

# 인터럽트 상태 확인
print intr_get_level()
```

---

## 5. CLion vs VSCode 비교

항목CLionVSCode**첫 세팅 난이도**중 (GUI 다이얼로그)중 (json 직접)**변수 펼쳐보기**매우 직관적 (구조체 ▶)좋음**메모리 뷰**강력 (전용 탭)확장 통해 가능**디스어셈블리**통합통합**레지스터 뷰**통합 (탭)디버그 콘솔 명령으로**Logpoint UI**다이얼로그 명확"Edit Breakpoint" 메뉴**조건부 중단점**우클릭 → 조건우클릭 → Conditional**GDB 콘솔 직접 접근**"GDB" 탭디버그 콘솔 (`-exec` prefix)**PintOS 매크로 (**`btthread` **등)**`.gdbinit` 로 추가 가능동일**Watches 영구 저장**`.idea/` 에 자동`launch.json` 에 명시적**Dev Container 통합**1 급 시민 (Mac UI + 컨테이너 백엔드)Remote-Containers 확장**메모리 사용량**큼 (수 GB)작음 (수백 MB)**무료 여부**학생/오픈소스 무료, 그 외 유료무료

**일반적 권장**:

- **익숙함이 우선** → 평소 쓰는 IDE.
- **디버깅 자체에 집중** → CLion (변수창·메모리 뷰가 강력).
- **가벼움·확장성** → VSCode (json 으로 무한 커스터마이즈).
- **여러 PintOS 프로젝트 / 다른 OS 강의** → CLion (Symbol/Path 관리 편함).

---

## 6. 트러블슈팅 — 빈출 문제 모음

### 6.1 환경

증상원인해결`pintos: not found`activate 미실행Makefile 에 `$(ACTIVATE)` prefix, 또는 `source pintos/activateos.dsk cannot be temporal`--gdb 모드인데 build 디렉터리 밖에서 실행`cd pintos/threads/build` 후 실행컨테이너 ID 매번 바뀜Dev Container 재생성영구 파일은 볼륨 안 (`/IdeaProjects/...`) 에 두기`~/.gdbinit` 사라짐컨테이너 재생성`.devcontainer/devcontainer.json` 의 `postCreateCommand` 에 등록

### 6.2 빌드

증상원인해결`kernel.o not found`빌드 안 됨`cd pintos/threads && makemake[1]: ... is up to date`캐시`.result`, `.output` 삭제 후 재실행중단점이 엉뚱한 줄최적화 (`-O2`) 인라인`Make.config` 에서 `-O0 -g` 확인

### 6.3 디버거 연결

증상원인해결`Connection refused`QEMU 안 떠 있음`make gdb T=...` 먼저 실행즉시 disconnect`.gdbinit`/Watches 의 위험 표현식`thread_current()` 등 제거, `ticks` 만`Remote 'g' packet too long`gdb 가 x86_64 모름`gdb-multiarch` 또는 `x86_64-elf-gdb` 사용Symbol file not found경로 오류컨테이너 경로 (`/IdeaProjects/...`) 확인

### 6.4 중단점

증상원인해결중단점 회색 X심볼/파일 매핑 문제빌드 확인, path mapping (VSCode) 또는 컨테이너 경로 (CLion)중단점 안 멈춤그 코드가 실행 안 되거나 인라인됨Logpoint 로 진입 여부 확인무한루프 (sleep) 안 빠져나옴busy-wait 라 매번 yieldPause 버튼으로 강제 중단

### 6.5 IDE 별 특이 케이스

**CLion**

- F5 가 아무 동작도 안 함 → 키맵 (`vsDebug` vs `macOS`) 확인
- Pintos GDB 실행 안 함 → 오른쪽 위 드롭다운에 선택돼 있는지
- 매 세션마다 Watches 다시 등록 → `.idea/` 가 git 에 커밋됐는지 확인 (안 됐으면 새 클론마다 사라짐)

**VSCode**

- F5 가 다른 동작 → keybindings.json 충돌 확인
- 디버그 시작 시 콘솔 출력 안 보임 → "Debug Console" 패널 열기
- Logpoint `{var}` 가 동작 안 함 → C/C++ 확장 버전 확인

---

## 7. 실전 시나리오 — Alarm Clock 디버깅 30 분 튜토리얼

### 7.1 목표

`timer_sleep()` 의 busy-wait 동작을 직접 관찰하고, 왜 `thread_block()` 으로 바꿔야 하는지 체감.

### 7.2 사전 준비

이 가이드의 §1, §2 (CLion) 또는 §3 (VSCode) 까지 완료된 상태.

### 7.3 단계

**Step 1 — QEMU 기동**

```bash
make gdb T=alarm-multiple
```

**Step 2 — 중단점 4 개 찍기**

(파일을 열고 거터 클릭 또는 F9)

파일:줄의도`tests/threads/tests.c:56` (`t->function ();`)테스트 dispatch 마지막`tests/threads/alarm-wait.c` `sleeper` 함수 첫 줄sleeper 스레드 진입`devices/timer.c:93` (`int64_t start = ...`)timer_sleep 진입`devices/timer.c:97` (`thread_yield ();`)busy-wait 의 yield 호출

**Step 3 — 디버그 시작**

CLion: 🐞 클릭 / VSCode: F5.

QEMU 의 BIOS 첫 명령어 (`0xfff0`) 에서 멈춘 상태로 디버거가 붙음.

**Step 4 — Resume → 첫 번째 중단점**

F5 또는 Resume → `tests.c:56` 에서 멈춤.

확인:

- **Frames 패널**: `run_test` (현재) → `run_task` → `run_actions` → `main`
- **변수 (Watches)**: `name = "alarm-multiple"`, `t = 0x800421e650`
- 이 시점 안전 — Watches 에 `thread_current()->name` 추가 해도 OK

**Step 5 — Step Into (F11/F7) → test_alarm_multiple**

`t->function ();` 안으로 들어감. `alarm-wait.c` 의 `test_alarm_multiple` → `test_sleep(5, 7)` 흐름.

**Step 6 — 다음 중단점까지 (F5)**

`sleeper` 함수에서 멈춤. **이때 변수 패널의** `thread_current()->name` **을 확인**:

```
thread_current()->name = "thread 0"     ← 첫 sleeper
```

다시 F5 → `thread 1`, `thread 2`, ... 5 개 sleeper 가 차례로 진입.

**Step 7 — timer_sleep 호출**

각 sleeper 가 곧 `timer_sleep` 을 부른다 → `timer.c:93` 에서 멈춤.

확인:

- `thread_current()->name = "thread N"`
- 인자 `ticks = ?` (스레드 N 의 duration 만큼)

**Step 8 — busy-wait 루프 관찰**

F8 (Step Over) 로 진행 → `while (timer_elapsed (start) < ticks)` 줄에서 평가.

`thread_yield ();` 줄에 도달 (97 번 줄, 중단점 있음).

**여기가 핵심**: 이 줄에서 F11 (Step Into) → `thread_yield()` 함수 안으로.

```c
void thread_yield (void) {
    struct thread *curr = thread_current ();
    enum intr_level old_level;
    
    ASSERT (!intr_context ());
    
    old_level = intr_disable ();
    if (curr != idle_thread)
        list_push_back (&ready_list, &curr->elem);  // ← READY 큐로 다시 들어감
    do_schedule (THREAD_READY);                      // ← 스케줄러 호출
    intr_set_level (old_level);
}
```

여기서 **이 스레드가 ready 큐로 다시 들어가는 것** 을 확인. 이게 핵심 — 잠시 후 다시 스케줄되어 `timer_sleep` 의 while 조건 검사로 돌아옴 → 또 yield → ... 무한 반복.

**Step 9 — Logpoint 로 반복 횟수 측정**

97 번 줄 (`thread_yield ();`) 의 중단점을 **Logpoint 로 변환**:

CLion: 우클릭 → "더 보기" → Suspend 해제 + Log "yield from %s @t=%lld" 등록. VSCode: 우클릭 → Edit Breakpoint → Log Message: `yield from {thread_current()->name} at {ticks}`

이제 F5 로 alarm-multiple 끝까지 진행. **콘솔에 yield 가 수백\~수천 번 찍힘**. 이게 busy-wait 의 비용.

### 7.4 학습 포인트 정리

- `thread_yield()` = ready 상태로 즉시 복귀. 잠시 후 또 스케줄. 같은 루프 반복.
- **CPU 사용률 100%** — idle 진입 못 함, 다른 작업 못 함.
- **출력 결과는 같음** — 채점 통과는 가능하지만 실용적이지 않음.
- **해결책**: `thread_block()` 으로 BLOCKED 상태 → ready 큐에서 빠짐 → 스케줄러가 안 부름. timer_interrupt 가 깨우는 시각이 오면 `thread_unblock()`.

이 변경이 바로 Alarm Clock 과제의 본체. 이론만 보면 추상적이지만 **위 디버깅을 거치고 나면 손에 잡힌다**.

---

## 8. 다음 단계로

이 디버그 환경이 잡히면 PintOS 의 다른 모든 과제 (Priority Scheduling, MLFQS, User Programs, VM, Filesys) 도 같은 방식으로 접근 가능.

학습 추천 순서:

1. 이 가이드대로 환경 세팅 (1 회)
2. `05-alarm-clock.md` (개념) + 디버거 (체험) 동시 진행
3. 코드 수정 → `make result T=alarm-multiple` 로 검증 → 디버거로 잔여 버그 추적
4. PASS 후 다음 테스트 (`alarm-priority`) 로
5. Priority 도 동일 패턴 — [06-priority-scheduling.md](http://06-priority-scheduling.md) 참고

---

## 부록 A — 단축키 비교

동작macOS 기본Visual Studio (vsDebug)VSCodeResume / Continue`F9F5F5`Stop / End session`⌘F2⇧F5⇧F5`Step Over`F8F10F10`Step Into`F7F11F11`Step Out`⇧F8⇧F11⇧F11`Toggle Breakpoint`⌘F8F9F9`Run to Cursor`⌥F9⌃F10`(없음)Evaluate Expression`⌥F8⇧F9`(디버그 콘솔)Restart Debug`⌃⌥R⌃⇧F5⌘⇧F5`

(`⌃` = Control, `⌥` = Option/Alt, `⌘` = Command, `⇧` = Shift)

## 부록 B — Mac 시스템 키 충돌

VSCode/VS 키맵 사용 시 macOS 기본 동작과 충돌 우려. 권장 조정:

```
시스템 설정 → 키보드 → 키보드 단축키
  → 기능 키 → "F1, F2 등의 키를 표준 기능 키로 사용" ✓
  → Mission Control → "Show Desktop" (F11) 체크 해제 또는 다른 키로
```

체크 안 하면 `F5`/`F10`/`F11` 누를 때마다 미디어 키 (밝기 조절 등) 가 동작해서 디버그가 안 됨.

## 부록 C — 권장 Watches 세트

부팅 완료 후 안전한 시점에서 추가:

```
ticks
intr_get_level()
thread_current()->name
thread_current()->status
thread_current()->priority
thread_current()->tid
list_size(&ready_list)
```

Alarm Clock 구현 후 추가:

```
list_size(&sleep_list)
((struct thread *) list_entry(list_front(&sleep_list), struct thread, elem))->wakeup_tick
```

Priority Donation 구현 후 추가:

```
thread_current()->original_priority
thread_current()->wait_on_lock
list_size(&thread_current()->donations)
```

## 부록 D — 권장 Logpoint 세트

#### 흐름 추적용 (alarm-multiple)

위치메시지 (CLion `printf` 식)메시지 (VSCode `{}` 식)`timer_sleep` 첫 줄`"sleep: %s wants %lld @t=%lld\n", thread_current()->name, ticks, timer_ticks()sleep: {thread_current()->name} wants {ticks} at {timer_ticks()}timer_interrupt` 안 (조건부 권장)`"tick %lld -> %s\n", ticks, thread_current()->nametick {ticks} -> {thread_current()->name}thread_yield` 첫 줄`"yield: %s\n", thread_current()->nameyield: {thread_current()->name}thread_block` 첫 줄`"block: %s\n", thread_current()->nameblock: {thread_current()->name}thread_unblock` 첫 줄`"unblock: %s\n", t->nameunblock: {t->name}`

`timer_interrupt` 는 1 초당 100 회 호출되니 무조건 logpoint 박으면 콘솔이 너무 시끄러워진다. 조건부:

- CLion: 조건 `ticks % 50 == 0` 추가 → 0.5 초마다만 출력
- VSCode: Conditional logpoint 사용

## 부록 E — `.gdbinit` 권장 템플릿 (안전 버전)

```gdb
# ~/.gdbinit (또는 build/.gdbinit)

# ─── 출력 형식 ───
set print pretty on
set print array on
set pagination off
set confirm off

# ─── 안전 설정 ───
add-auto-load-safe-path /

# ─── 자동 표시 — 항상 안전한 것만 ───
display ticks

# ─── 사용자 매크로 (선택) ───
define dump_thread
    printf "name=%s status=%d priority=%d tid=%d\n", \
        thread_current()->name, \
        thread_current()->status, \
        thread_current()->priority, \
        thread_current()->tid
end

define dump_ready
    printf "ready_list size: %d\n", list_size(&ready_list)
end

# 사용법: GDB 콘솔에서 `dump_thread` 또는 `dump_ready` 입력
```

매크로는 부팅 후 안전한 시점에서만 호출. `display` 와 달리 자동 평가 안 됨 → 안전.

## 부록 F — 컨테이너 재생성 대비 영구화 팁

Dev Container 가 매번 새로 만들어지는 환경 (특히 JetBrains Dev Container) 에서는 컨테이너 안에 만든 파일이 사라진다. 영구 유지 방법:

### F.1 볼륨 마운트 안에 두기

호스트와 컨테이너가 공유하는 위치 (= 프로젝트 폴더) 안에 두면 영구.

```bash
# 컨테이너에서:
cp ~/.gdbinit /IdeaProjects/SW_AI-W09-pintos/pintos/threads/build/.gdbinit
```

GDB 가 작업 디렉터리 (`build/`) 의 `.gdbinit` 도 자동 로드 (단, safe-path 등록 필요).

### F.2 `.devcontainer/devcontainer.json` 의 `postCreateCommand`

컨테이너 생성 직후 자동 실행할 명령:

```json
{
  "postCreateCommand": "cp /workspaces/SW_AI-W09-pintos/.devcontainer/.gdbinit ~/.gdbinit"
}
```
```

`.devcontainer/.gdbinit` 에 권장 템플릿 두고, 매 컨테이너 생성 시 자동 복사.

### F.3 호스트에 두고 마운트 추가 (권장 X)

`.devcontainer/devcontainer.json` 에 mount 추가는 가능하지만 컨테이너 이미지·버전 의존성 생겨서 권장 안 함.

---

## 부록 G — 문제 진단 플로우차트

```
디버그가 안 됨
   │
   ├─ "Connection refused"
   │     → QEMU 실행 중인가?  → make gdb T=...
   │
   ├─ "즉시 disconnect"
   │     → .gdbinit / Watches 의 위험 표현식 제거
   │
   ├─ 중단점 회색 X
   │     → 빌드 됐나?  → make
   │     → 심볼 파일 경로 맞나?  → 컨테이너 경로 (/IdeaProjects/...)
   │     → 다른 프로젝트의 동명 파일?
   │
   ├─ 중단점 활성인데 안 멈춤
   │     → 그 코드가 실행되나?  → Logpoint 로 진입 확인
   │     → 최적화로 인라인됐나?  → -O0 -g 확인
   │
   ├─ F5 / 단축키 무반응
   │     → 키맵 확인 (CLion: vsDebug 인지)
   │     → Mac 시스템 단축키 충돌
   │     → 마우스로 직접 클릭 시도
   │
   └─ "pintos: not found"
         → source pintos/activate
         → Makefile 에 $(ACTIVATE) 있나?
```

---

## 변경 이력

| 날짜 | 변경 내용 |
| --- | --- |
| 2026-04-25 | 초판 — CLion 2026.1, VSCode 최신 기준. Dev Container 모드 중심 |
