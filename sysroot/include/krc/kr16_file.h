#pragma once

/* The KR16 file stream record.
 *
 * `<stdio.h>` declares the standard stream functions for the KR16 profile; this
 * header owns the record they work on, because its layout is sixteen-bit and
 * the KR32 record is not. A program includes `<stdio.h>` and needs this header
 * only when it wants the open flags by name.
 *
 * The record is a fixed-size slot from a static pool: sixteen of them, matching
 * the VM's open-file limit, with no allocation. `in_use` marks a live slot
 * because the VM hands out descriptor 0, so a negative descriptor cannot.
 */

struct krc16_file {
    int in_use;        /* non-zero while a slot holds a stream */
    int descriptor;    /* the VM descriptor; a descriptor of 0 is valid */
    int writable;      /* non-zero when the stream was opened for writing */
    int error;         /* set by a failed transfer */
    int end_of_file;   /* set when a read reached the end */
    int pushback;      /* a byte returned by ungetc, or -1 */
    long position;     /* the stream's own byte position */
};

typedef struct krc16_file FILE;

#define KRC16_OPEN_READ 0
#define KRC16_OPEN_WRITE 1
#define KRC16_OPEN_READ_WRITE 2
#define KRC16_OPEN_CREATE 0x0100
#define KRC16_OPEN_TRUNCATE 0x0200
#define KRC16_OPEN_APPEND 0x0400
#define KRC16_OPEN_EXCLUSIVE 0x0800

/* The console streams. `<stdio.h>` defines stdout and stderr as these calls:
   a data pointer whose initialiser needs a relocation is not available on
   this profile, and a lazily initialised static record is zero-filled by the
   image. The runtime returns a stream over descriptor 1 and 2 that is never
   closed. */
FILE* __krc16_stdout(void);
FILE* __krc16_stderr(void);
