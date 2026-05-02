#include <string.h>
#include <debug.h>

/* @lock
 * 겹치지 않는 SRC의 SIZE바이트를 DST로 복사한다.
 * DST를 반환한다.
 */
void *
memcpy (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	while (size-- > 0)
		*dst++ = *src++;

	return dst_;
}

/* @lock
 * 겹쳐도 되는 SRC의 SIZE바이트를 DST로 복사한다.
 * DST를 반환한다.
 */
void *
memmove (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	if (dst < src) {
		while (size-- > 0)
			*dst++ = *src++;
	} else {
		dst += size;
		src += size;
		while (size-- > 0)
			*--dst = *--src;
	}

	return dst;
}

/* @lock
 * A와 B의 SIZE바이트 블록을 비교해 처음으로 다른 바이트를 찾는다.
 * A의 바이트가 더 크면 양수를, B의 바이트가 더 크면 음수를,
 * 두 블록이 같으면 0을 반환한다.
 */
int
memcmp (const void *a_, const void *b_, size_t size) {
	const unsigned char *a = a_;
	const unsigned char *b = b_;

	ASSERT (a != NULL || size == 0);
	ASSERT (b != NULL || size == 0);

	for (; size-- > 0; a++, b++)
		if (*a != *b)
			return *a > *b ? +1 : -1;
	return 0;
}

/* @lock
 * 문자열 A와 B에서 처음으로 다른 문자를 찾는다.
 * A의 문자(unsigned char 기준)가 더 크면 양수를,
 * B의 문자가 더 크면 음수를, 두 문자열이 같으면 0을 반환한다.
 */
int
strcmp (const char *a_, const char *b_) {
	const unsigned char *a = (const unsigned char *) a_;
	const unsigned char *b = (const unsigned char *) b_;

	ASSERT (a != NULL);
	ASSERT (b != NULL);

	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}

	return *a < *b ? -1 : *a > *b;
}

/* @lock
 * BLOCK부터 시작하는 처음 SIZE바이트에서 CH가 처음 나타나는 위치를
 * 가리키는 포인터를 반환한다.
 * BLOCK 안에 CH가 없으면 널 포인터를 반환한다.
 */
void *
memchr (const void *block_, int ch_, size_t size) {
	const unsigned char *block = block_;
	unsigned char ch = ch_;

	ASSERT (block != NULL || size == 0);

	for (; size-- > 0; block++)
		if (*block == ch)
			return (void *) block;

	return NULL;
}

/* @lock
 * STRING에서 C가 처음 나타나는 위치를 찾아 반환한다.
 * C가 STRING에 없으면 널 포인터를 반환한다.
 * C == '\0'이면 STRING 끝의 널 종료 문자를 가리키는 포인터를 반환한다.
 */
char *
strchr (const char *string, int c_) {
	char c = c_;

	ASSERT (string);

	for (;;)
		if (*string == c)
			return (char *) string;
		else if (*string == '\0')
			return NULL;
		else
			string++;
}

/* @lock
 * STRING의 앞부분에서 STOP에 포함되지 않은 문자들로만 이루어진
 * 초기 부분 문자열의 길이를 반환한다.
 */
size_t
strcspn (const char *string, const char *stop) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (stop, string[length]) != NULL)
			break;
	return length;
}

/* @lock
 * STRING에서 STOP에도 포함된 첫 문자를 가리키는 포인터를 반환한다.
 * STRING의 어떤 문자도 STOP에 없으면 널 포인터를 반환한다.
 */
char *
strpbrk (const char *string, const char *stop) {
	for (; *string != '\0'; string++)
		if (strchr (stop, *string) != NULL)
			return (char *) string;
	return NULL;
}

/* @lock
 * STRING에서 C가 마지막으로 나타나는 위치를 가리키는 포인터를 반환한다.
 * C가 STRING에 없으면 널 포인터를 반환한다.
 */
char *
strrchr (const char *string, int c_) {
	char c = c_;
	const char *p = NULL;

	for (; *string != '\0'; string++)
		if (*string == c)
			p = string;
	return (char *) p;
}

/* @lock
 * STRING의 앞부분에서 SKIP에 포함된 문자들로만 이루어진
 * 초기 부분 문자열의 길이를 반환한다.
 */
size_t
strspn (const char *string, const char *skip) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (skip, string[length]) == NULL)
			break;
	return length;
}

/* @lock
 * HAYSTACK 안에서 NEEDLE이 처음 나타나는 위치를 가리키는 포인터를
 * 반환한다.
 * HAYSTACK 안에 NEEDLE이 없으면 널 포인터를 반환한다.
 */
char *
strstr (const char *haystack, const char *needle) {
	size_t haystack_len = strlen (haystack);
	size_t needle_len = strlen (needle);

	if (haystack_len >= needle_len) {
		size_t i;

		for (i = 0; i <= haystack_len - needle_len; i++)
			if (!memcmp (haystack + i, needle, needle_len))
				return (char *) haystack + i;
	}

	return NULL;
}

/* @lock
 * DELIMITERS로 구분된 토큰들로 문자열을 나눈다.
 * 이 함수를 처음 호출할 때는 S에 토큰화할 문자열을 넘겨야 하고,
 * 그 이후 호출에서는 S가 널 포인터여야 한다.
 * SAVE_PTR은 토크나이저의 현재 위치를 추적하는 `char *` 변수의 주소다.
 * 각 호출의 반환값은 문자열의 다음 토큰이며,
 * 남은 토큰이 없으면 널 포인터를 반환한다.
 *
 * 이 함수는 연속된 여러 구분자를 하나의 구분자로 취급한다.
 * 반환되는 토큰의 길이는 절대 0이 아니다.
 * 하나의 문자열을 처리하는 동안에는 호출마다 DELIMITERS가 달라도 된다.
 *
 * strtok_r()는 문자열 S를 수정해 구분자를 널 바이트로 바꾼다.
 * 따라서 S는 수정 가능한 문자열이어야 한다.
 * 특히 문자열 리터럴은, 하위 호환 때문에 `const`가 아니더라도,
 * C에서 수정할 수 없다.
 *
 * 사용 예:
 *
 * char s[] = "  String to  tokenize. ";
 * char *token, *save_ptr;
 *
 * for (token = strtok_r (s, " ", &save_ptr); token != NULL;
 *      token = strtok_r (NULL, " ", &save_ptr))
 *   printf ("'%s'\n", token);
 *
 * 출력:
 *
 * 'String'
 * 'to'
 * 'tokenize.'
 */
char *
strtok_r (char *s, const char *delimiters, char **save_ptr) {
	char *token;

	ASSERT (delimiters != NULL);
	ASSERT (save_ptr != NULL);

	/* @lock
	 * S가 널이 아니면 S부터 시작하고, 널이면 저장된 위치부터 시작한다.
	 */
	if (s == NULL)
		s = *save_ptr;
	ASSERT (s != NULL);

	/* @lock
	 * 현재 위치에서 DELIMITERS에 포함된 문자를 모두 건너뛴다.
	 */
	while (strchr (delimiters, *s) != NULL) {
		/* @lock
		 * 문자열은 끝에 항상 널 바이트를 가지므로,
		 * 널 바이트를 찾을 때 strchr()는 항상 널이 아닌 값을 반환한다.
		 */
		if (*s == '\0') {
			*save_ptr = s;
			return NULL;
		}

		s++;
	}

	/* @lock
	 * 문자열 끝까지 DELIMITERS에 포함되지 않은 문자를 건너뛴다.
	 */
	token = s;
	while (strchr (delimiters, *s) == NULL)
		s++;
	if (*s != '\0') {
		*s = '\0';
		*save_ptr = s + 1;
	} else
		*save_ptr = s;
	return token;
}

/* @lock
 * DST의 SIZE바이트를 VALUE로 채운다.
 */
void *
memset (void *dst_, int value, size_t size) {
	unsigned char *dst = dst_;

	ASSERT (dst != NULL || size == 0);

	while (size-- > 0)
		*dst++ = value;

	return dst_;
}

/* @lock
 * STRING의 길이를 반환한다.
 */
size_t
strlen (const char *string) {
	const char *p;

	ASSERT (string);

	for (p = string; *p != '\0'; p++)
		continue;
	return p - string;
}

/* @lock
 * STRING의 길이가 MAXLEN보다 짧으면 실제 길이를 반환한다.
 * 그렇지 않으면 MAXLEN을 반환한다.
 */
size_t
strnlen (const char *string, size_t maxlen) {
	size_t length;

	for (length = 0; string[length] != '\0' && length < maxlen; length++)
		continue;
	return length;
}

/* @lock
 * 문자열 SRC를 DST로 복사한다.
 * SRC가 SIZE - 1보다 길면 앞의 SIZE - 1글자만 복사한다.
 * SIZE가 0이 아닌 한, DST에는 항상 널 종료 문자가 기록된다.
 * 널 종료 문자를 제외한 SRC의 길이를 반환한다.
 *
 * strlcpy()는 표준 C 라이브러리에 포함되어 있지 않지만,
 * 점점 널리 쓰이는 확장이다.
 * strlcpy()에 대한 정보는
 * http://www.courtesan.com/todd/papers/strlcpy.html 를 참고하라.
 */
size_t
strlcpy (char *dst, const char *src, size_t size) {
	size_t src_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	if (size > 0) {
		size_t dst_len = size - 1;
		if (src_len < dst_len)
			dst_len = src_len;
		memcpy (dst, src, dst_len);
		dst[dst_len] = '\0';
	}
	return src_len;
}

/* @lock
 * 문자열 SRC를 DST 뒤에 이어 붙인다.
 * 이어 붙인 결과 문자열은 최대 SIZE - 1글자로 제한된다.
 * SIZE가 0이 아닌 한, DST에는 항상 널 종료 문자가 기록된다.
 * 공간이 충분하다고 가정했을 때 만들어졌을 전체 문자열 길이를,
 * 널 종료 문자를 제외하고 반환한다.
 *
 * strlcat()는 표준 C 라이브러리에 포함되어 있지 않지만,
 * 점점 널리 쓰이는 확장이다.
 * strlcpy()에 대한 정보는
 * http://www.courtesan.com/todd/papers/strlcpy.html 를 참고하라.
 */
size_t
strlcat (char *dst, const char *src, size_t size) {
	size_t src_len, dst_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	dst_len = strlen (dst);
	if (size > 0 && dst_len < size) {
		size_t copy_cnt = size - dst_len - 1;
		if (src_len < copy_cnt)
			copy_cnt = src_len;
		memcpy (dst + dst_len, src, copy_cnt);
		dst[dst_len + copy_cnt] = '\0';
	}
	return src_len + dst_len;
}
