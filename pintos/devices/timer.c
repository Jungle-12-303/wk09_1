#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* @lock 8254 타이머 칩의 하드웨어 세부 사항은 [8254]를 참고한다. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* @lock OS가 부팅된 이후 지난 타이머 틱 수. */
static int64_t ticks;

/* @lock 타이머 틱 하나당 루프 횟수.
   timer_calibrate()에서 초기화된다. */
static unsigned loops_per_tick;
static struct list sleep_list;


static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

// static struct list sleep_list;
 
/* @lock 8254 프로그래머블 인터벌 타이머(PIT)가 초당 PIT_FREQ번
   인터럽트를 발생시키도록 설정하고, 해당 인터럽트를 등록한다. */
void
timer_init (void) {
	/* @lock 8254 입력 주파수를 TIMER_FREQ로 나눈 값을 가장 가까운 값으로 반올림한다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* @lock 제어 워드: 카운터 0, LSB 다음 MSB, 모드 2, 바이너리. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	list_init(&sleep_list);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* @lock 짧은 지연을 구현하는 데 사용하는 loops_per_tick을 보정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* @lock loops_per_tick을 타이머 틱 하나보다 아직 작은 2의 거듭제곱 중
	   가장 큰 값으로 근사한다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* @lock loops_per_tick의 다음 8비트를 세밀하게 조정한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* @lock OS가 부팅된 이후 지난 타이머 틱 수를 반환한다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* @lock THEN 이후 경과한 타이머 틱 수를 반환한다.
   THEN은 이전에 timer_ticks()가 반환한 값이어야 한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* @lock 대략 TICKS 타이머 틱 동안 실행을 중단한다. */
void
timer_sleep (int64_t ticks) {
	if (ticks <= 0) return;

	ASSERT (intr_get_level () == INTR_ON);
	thread_sleep (timer_ticks () + ticks);
}

/* @lock 대략 MS 밀리초 동안 실행을 중단한다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* @lock 대략 US 마이크로초 동안 실행을 중단한다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* @lock 대략 NS 나노초 동안 실행을 중단한다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* @lock 타이머 통계를 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* @lock 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;    
	thread_tick ();

	thread_awake (ticks);
}

/* @lock LOOPS번 반복하는 데 타이머 틱 하나보다 오래 걸리면 true를,
   그렇지 않으면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* @lock 타이머 틱 하나를 기다린다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* @lock LOOPS번 루프를 실행한다. */
	start = ticks;
	busy_wait (loops);

	/* @lock 틱 카운트가 바뀌었다면 반복 시간이 너무 길었던 것이다. */
	barrier ();
	return start != ticks;
}

/* @lock 짧은 지연을 구현하기 위해 단순 루프를 LOOPS번 반복한다.

   코드 정렬이 시간 측정에 큰 영향을 줄 수 있으므로 NO_INLINE으로 표시한다.
   이 함수가 위치마다 다르게 인라인되면 결과를 예측하기 어려워진다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* @lock 대략 NUM/DENOM초 동안 잔다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* @lock NUM/DENOM초를 타이머 틱 수로 변환하되, 내림한다.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* @lock 최소 한 번의 완전한 타이머 틱 이상을 기다리는 중이다.
		   CPU를 다른 프로세스에 양보하므로 timer_sleep()을 사용한다. */
		timer_sleep (ticks);
	} else {
		/* @lock 그렇지 않으면 더 정확한 틱 미만 시간 측정을 위해 busy-wait 루프를 사용한다.
		   오버플로우 가능성을 피하려고 분자와 분모를 1000으로 줄인다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
