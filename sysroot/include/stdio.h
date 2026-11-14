#ifndef KRC_STDIO_H
#define KRC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __kr16__
/* The KR16 profile has its own stream record and a subset of the stream
   functions; targets/kr16/runtime/fileio.c provides them over the host
   services. The set that is missing is named at the end of this header. */
#include <krc/kr16_file.h>
#else
typedef struct FILE FILE;
#endif

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define L_tmpnam 32
#define TMP_MAX 25
#define BUFSIZ 512

#ifdef __kr16__
/* The KR16 profile provides the console functions from the runtime library and
   the stream functions from targets/kr16/runtime/fileio.c. A function that is
   not declared here does not exist on this profile; `stdin` is read with
   getchar, and there is no formatted stream output beyond printf. */
int printf(const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsprintf(char *buffer, const char *format, va_list args);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int putchar(int c);
int puts(const char *s);
int getchar(void);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int ungetc(int c, FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);

#define stdout (__krc16_stdout())
#define stderr (__krc16_stderr())
#else
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
int dprintf(int descriptor, const char *format, ...);
int vdprintf(int descriptor, const char *format, va_list args);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int vsprintf(char *buffer, const char *format, va_list args);

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
int getdelim(char **line, size_t *size, int delimiter, FILE *stream);
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
FILE *fdopen(int descriptor, const char *mode);
int fileno(FILE *stream);
char *tmpnam(char *buffer);
int fflush(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

/* Host service: shows the platform's open-file dialog and writes the chosen
   path into buffer. Returns the length written, or -1 when cancelled. */
int sys_open_dialog(char *buffer, int size);

#endif  /* __kr16__ */
#endif
