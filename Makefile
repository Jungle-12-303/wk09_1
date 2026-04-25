# ============================================================
# Pintos Project 1 — 편의용 Makefile
# ============================================================
# 사용법:
#   make              빌드
#   make run T=alarm-multiple   테스트 실행
#   make check        전체 테스트
#   make result T=alarm-multiple  개별 테스트 채점
#   make gdb T=alarm-single     GDB 디버깅 (터미널1)
#   make gdb-attach             GDB 연결 (터미널2)
#   make clean        빌드 삭제
# ============================================================

SHELL := /bin/bash

THREADS_DIR = pintos/threads
BUILD_DIR   = $(THREADS_DIR)/build
ACTIVATE    = source pintos/activate &&

# 기본: 빌드
all: build

# 빌드
build:
	cd $(THREADS_DIR) && make

# 개별 테스트 실행 (예: make run T=alarm-multiple)
run:
ifndef T
	@echo "사용법: make run T=<테스트이름>"
	@echo "예시:   make run T=alarm-multiple"
	@echo ""
	@echo "=== Alarm Clock ==="
	@echo "  alarm-single  alarm-multiple  alarm-simultaneous"
	@echo "  alarm-priority  alarm-zero  alarm-negative"
	@echo ""
	@echo "=== Priority Scheduling ==="
	@echo "  priority-change  priority-preempt  priority-fifo"
	@echo "  priority-sema  priority-condvar"
	@echo ""
	@echo "=== Priority Donation ==="
	@echo "  priority-donate-one  priority-donate-multiple"
	@echo "  priority-donate-multiple2  priority-donate-nest"
	@echo "  priority-donate-chain  priority-donate-sema"
	@echo "  priority-donate-lower"
	@echo ""
	@echo "=== MLFQS ==="
	@echo "  mlfqs-load-1  mlfqs-load-60  mlfqs-load-avg"
	@echo "  mlfqs-recent-1  mlfqs-fair-2  mlfqs-fair-20"
	@echo "  mlfqs-nice-2  mlfqs-nice-10  mlfqs-block"
else
	$(ACTIVATE) cd $(BUILD_DIR) && pintos -- -q run $(T)
endif

# 개별 테스트 채점 (예: make result T=alarm-multiple)
result:
ifndef T
	@echo "사용법: make result T=<테스트이름>"
	@echo "예시:   make result T=alarm-multiple"
else
	$(ACTIVATE) cd $(BUILD_DIR) && make tests/threads/$(T).result
endif

# MLFQS 테스트 실행 (예: make mlfqs T=mlfqs-load-1)
mlfqs:
ifndef T
	@echo "사용법: make mlfqs T=<테스트이름>"
	@echo "예시:   make mlfqs T=mlfqs-load-1"
else
	$(ACTIVATE) cd $(BUILD_DIR) && pintos -- -q -mlfqs run $(T)
endif

# 전체 테스트
check:
	$(ACTIVATE) cd $(THREADS_DIR) && make check

# 빌드 후 바로 실행 (예: make br T=alarm-multiple)
br:
ifndef T
	@echo "사용법: make br T=<테스트이름>"
else
	cd $(THREADS_DIR) && make
	$(ACTIVATE) cd $(BUILD_DIR) && pintos -- -q run $(T)
endif

# GDB 디버깅 (예: make gdb T=alarm-single)
# 터미널1에서 실행하면 QEMU가 GDB 연결을 기다린다.
# 터미널2에서 make gdb-attach 로 연결한다.
gdb:
ifndef T
	@echo "사용법: make gdb T=<테스트이름>"
	@echo "예시:   make gdb T=alarm-single"
	@echo ""
	@echo "1) 터미널1: make gdb T=alarm-single"
	@echo "2) 터미널2: make gdb-attach"
	@echo "3) GDB에서: break main → continue → next"
else
	$(ACTIVATE) cd $(BUILD_DIR) && pintos --gdb -- -q run $(T)
endif

# GDB 연결 (다른 터미널에서 실행)
gdb-attach:
	cd $(BUILD_DIR) && gdb -q kernel.o \
		-x .gdbinit \
		-ex "target remote localhost:1234" \
		-ex "continue"

# 클린
clean:
	cd $(THREADS_DIR) && make clean

.PHONY: all build run result mlfqs check br gdb gdb-attach clean
