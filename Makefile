# ============================================================
# Pintos Project 1 — 편의용 Makefile
# ============================================================
# 사용법:
#   make              빌드
#   make run T=alarm-multiple   테스트 실행
#   make check        전체 테스트
#   make result T=alarm-multiple  개별 테스트 채점
#   make clean        빌드 삭제
# ============================================================

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
	cd $(BUILD_DIR) && pintos -- -q run $(T)
endif

# 개별 테스트 채점 (예: make result T=alarm-multiple)
result:
ifndef T
	@echo "사용법: make result T=<테스트이름>"
	@echo "예시:   make result T=alarm-multiple"
else
	cd $(THREADS_DIR) && make tests/threads/$(T).result
endif

# MLFQS 테스트 실행 (예: make mlfqs T=mlfqs-load-1)
mlfqs:
ifndef T
	@echo "사용법: make mlfqs T=<테스트이름>"
	@echo "예시:   make mlfqs T=mlfqs-load-1"
else
	cd $(BUILD_DIR) && pintos -- -q -mlfqs run $(T)
endif

# 전체 테스트
check:
	cd $(THREADS_DIR) && make check

# 빌드 후 바로 실행 (예: make br T=alarm-multiple)
br:
ifndef T
	@echo "사용법: make br T=<테스트이름>"
else
	cd $(THREADS_DIR) && make
	cd $(BUILD_DIR) && pintos -- -q run $(T)
endif

# 클린
clean:
	cd $(THREADS_DIR) && make clean

.PHONY: all build run result mlfqs check br clean
