#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int vasprintf(char **restrict strp, const char *restrict fmt, va_list va)
{
	va_list copy;

	va_copy(copy, va);
	int len = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	if (len < 0) {
		*strp = NULL;
		return len;
	}
	++len;

	*strp = malloc(len);
	if (!*strp)
		return -1;

	int ret = vsnprintf(*strp, len, fmt, va);

	if (ret < 0)
		free(*strp);

	return ret;
}

int asprintf(char **strp, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	int ret = vasprintf(strp, fmt, va);
	va_end(va);

	return ret;
}
