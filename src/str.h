#ifndef STR_H
#define STR_H

int vasprintf(char **strp, const char *fmt, va_list va);
int asprintf(char **strp, const char *fmt, ...);

#endif