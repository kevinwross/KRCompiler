#pragma once

/* A KR16 file stream over the host-service wrappers.
 *
 * The header is target-specific because a stream record is laid out for 16-bit
 * words and 32-bit positions; `<stdio.h>` for KR32 keeps its own layout. A
 * program that wants file I/O on KR16 includes this header and links
 * `libkrcrt_fileio.kro` together with `libkrcrt_host.kro`.
 *
 * Open flags are the VM's: bit 0 write, bit 1 read-write, 0x0100 create,
 * 0x0200 truncate, 0x0400 append, 0x0800 exclusive. `krc16_fopen` passes the
 * low twelve bits through.
 */

typedef struct krc16_file {
    int in_use;        /* non-zero while a slot holds a stream */
    int descriptor;    /* the VM descriptor; a descriptor of 0 is valid */
    int writable;      /* non-zero when the stream was opened for writing */
    int error;         /* set by a failed transfer */
    int end_of_file;   /* set when a read reached the end */
    int pushback;      /* a byte returned by ungetc, or -1 */
    long position;     /* the stream's own byte position */
} krc16_file;

krc16_file* krc16_fopen(const char* path, unsigned flags);
int krc16_fclose(krc16_file* stream);
unsigned krc16_fread(void* buffer, unsigned size, unsigned count, krc16_file* stream);
unsigned krc16_fwrite(const void* buffer, unsigned size, unsigned count,
                      krc16_file* stream);
int krc16_fgetc(krc16_file* stream);
int krc16_ungetc(int character, krc16_file* stream);
int krc16_fputc(int character, krc16_file* stream);
char* krc16_fgets(char* buffer, int capacity, krc16_file* stream);
int krc16_fputs(const char* text, krc16_file* stream);
long krc16_ftell(krc16_file* stream);
int krc16_fseek(krc16_file* stream, long offset, int origin);
int krc16_feof(krc16_file* stream);
int krc16_ferror(krc16_file* stream);
void krc16_clearerr(krc16_file* stream);
int krc16_remove(const char* path);
int krc16_rename(const char* from, const char* to);

/* The console streams: descriptor 1 and 2, never closed. */
krc16_file* krc16_stdout(void);
krc16_file* krc16_stderr(void);

#define KRC16_OPEN_READ 0
#define KRC16_OPEN_WRITE 1
#define KRC16_OPEN_READ_WRITE 2
#define KRC16_OPEN_CREATE 0x0100
#define KRC16_OPEN_TRUNCATE 0x0200
#define KRC16_OPEN_APPEND 0x0400
#define KRC16_OPEN_EXCLUSIVE 0x0800

#define KRC16_SEEK_SET 0
#define KRC16_SEEK_CUR 1
#define KRC16_SEEK_END 2
