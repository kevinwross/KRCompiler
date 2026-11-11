#ifndef KRC_STDIO_H
#define KRC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct FILE FILE;

#define EOF (-1)
#define L_tmpnam 32
#define TMP_MAX 25
#define BUFSIZ 512

FILE *__krc_stdin_stream(void);
FILE *__krc_stdout_stream(void);
FILE *__krc_stderr_stream(void);

#define stdin (__krc_stdin_stream())
#define stdout (__krc_stdout_stream())
#define stderr (__krc_stderr_stream())

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vprintf(const char *format, va_list args);
int vfprintf(FILE *stream, const char *format, va_list args);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);

int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int vsscanf(const char *input, const char *format, va_list args);
int vfscanf(FILE *stream, const char *format, va_list args);
int sscanf(const char *input, const char *format, ...);

int putchar(int c);
int puts(const char *s);
int getchar(void);
void perror(const char *s);

FILE *fopen(const char *path, const char *mode);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int getline(char **line, size_t *size, FILE *stream);
int getc(FILE *stream);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int ungetc(int c, FILE *stream);
typedef long fpos_t;
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
int setbuf(FILE *stream, char *buffer);
int fgetpos(FILE *stream, fpos_t *position);
int fsetpos(FILE *stream, const fpos_t *position);
FILE *freopen(const char *path, const char *mode, FILE *stream);
FILE *tmpfile(void);
char *tmpnam(char *buffer);
int fflush(FILE *stream);

#endif
