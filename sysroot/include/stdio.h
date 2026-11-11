#ifndef KRC_STDIO_H
#define KRC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct FILE FILE;

#define EOF (-1)
#define BUFSIZ 512

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vprintf(const char *format, va_list args);
int vfprintf(FILE *stream, const char *format, va_list args);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);

int scanf(const char *format, ...);
int sscanf(const char *input, const char *format, ...);

int putchar(int c);
int puts(const char *s);
int getchar(void);
void perror(const char *s);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int getline(char **line, size_t *size, FILE *stream);
int ungetc(int c, FILE *stream);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
int fflush(FILE *stream);

#endif
