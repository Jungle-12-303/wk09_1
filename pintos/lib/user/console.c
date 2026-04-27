#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <syscall-nr.h>

/* @lock 표준 vprintf() 함수.
   printf()와 같지만 va_list를 사용한다. */
int
vprintf (const char *format, va_list args) {
	return vhprintf (STDOUT_FILENO, format, args);
}

/* @lock printf()와 같지만 주어진 HANDLE에 출력을 쓴다. */
int
hprintf (int handle, const char *format, ...) {
	va_list args;
	int retval;

	va_start (args, format);
	retval = vhprintf (handle, format, args);
	va_end (args);

	return retval;
}

/* @lock 문자열 S를 콘솔에 쓰고, 이어서 줄바꿈 문자를 쓴다. */
int
puts (const char *s) {
	write (STDOUT_FILENO, s, strlen (s));
	putchar ('\n');

	return 0;
}

/* @lock C를 콘솔에 쓴다. */
int
putchar (int c) {
	char c2 = c;
	write (STDOUT_FILENO, &c2, 1);
	return c;
}

/* @lock vhprintf_helper()용 보조 데이터. */
struct vhprintf_aux {
	char buf[64];       /* @lock 문자 버퍼. */
	char *p;            /* @lock 버퍼 안의 현재 위치. */
	int char_cnt;       /* @lock 지금까지 쓴 총 문자 수. */
	int handle;         /* @lock 출력 파일 핸들. */
};

static void add_char (char, void *);
static void flush (struct vhprintf_aux *);

/* @lock ARGS로 주어진 인자로 printf() 형식 지정 FORMAT을 포맷하고
   출력을 주어진 HANDLE에 쓴다. */
int
vhprintf (int handle, const char *format, va_list args) {
	struct vhprintf_aux aux;
	aux.p = aux.buf;
	aux.char_cnt = 0;
	aux.handle = handle;
	__vprintf (format, args, add_char, &aux);
	flush (&aux);
	return aux.char_cnt;
}

/* @lock C를 AUX 안의 버퍼에 추가하고, 버퍼가 차면 비운다. */
static void
add_char (char c, void *aux_) {
	struct vhprintf_aux *aux = aux_;
	*aux->p++ = c;
	if (aux->p >= aux->buf + sizeof aux->buf)
		flush (aux);
	aux->char_cnt++;
}

/* @lock AUX 안의 버퍼를 비운다. */
static void
flush (struct vhprintf_aux *aux) {
	if (aux->p > aux->buf)
		write (aux->handle, aux->buf, aux->p - aux->buf);
	aux->p = aux->buf;
}
