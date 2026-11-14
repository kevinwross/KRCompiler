/* A file layer for KR16 over the host-service wrappers.
 *
 * The wrappers in `host.s` speak the VM's service block protocol; this file
 * gives them a stream interface so a KR16 program can open, read, write, and
 * seek a file without knowing the block layout. Descriptors 1 and 2 are the
 * console, which the VM writes directly, so a stream over them does not open a
 * file.
 *
 * A stream is a fixed-size record: no allocation, and `fopen` hands out a slot
 * from a static pool, so a program that leaks a stream cannot exhaust the heap.
 * Sixteen streams are the pool, matching the VM's open-file limit.
 */

#include <krc/kr16_file.h>

int krc16_sys_open(const char* path, unsigned flags);
int krc16_sys_close(int fd);
int krc16_sys_read(int fd, void* buffer, unsigned count);
int krc16_sys_write(int fd, const void* buffer, unsigned count);
long krc16_sys_seek(int fd, long offset);
int krc16_sys_unlink(const char* path);
int krc16_sys_rename(const char* from, const char* to);

#define KRC16_STREAM_COUNT 16
#define KRC16_SEEK_SET 0
#define KRC16_SEEK_CUR 1
#define KRC16_SEEK_END 2

static krc16_file streams[KRC16_STREAM_COUNT];

/* A console stream is addressed by its descriptor and never closed. */
static krc16_file console_out = {1, 1, 1, 0, 0, -1};
static krc16_file console_err = {1, 2, 1, 0, 0, -1};

krc16_file* krc16_stdout(void) { return &console_out; }
krc16_file* krc16_stderr(void) { return &console_err; }

static krc16_file* allocate_stream(void) {
    /* Walk with a pointer: the KR16 backend scales a struct step but not a
       dynamic index into an array of structs. */
    krc16_file* stream = streams;
    int index = 0;
    while (index < KRC16_STREAM_COUNT) {
        if (!stream->in_use) {
            stream->in_use = 1;
            stream->error = 0;
            stream->end_of_file = 0;
            stream->pushback = -1;
            return stream;
        }
        stream++;
        index++;
    }
    return 0;
}

krc16_file* krc16_fopen(const char* path, unsigned flags) {
    int descriptor = krc16_sys_open(path, flags & 0x0FFF);
    if (descriptor < 0) {
        return 0;
    }
    krc16_file* stream = allocate_stream();
    if (stream == 0) {
        krc16_sys_close(descriptor);
        return 0;
    }
    stream->descriptor = descriptor;
    stream->writable = (flags & 1u) != 0u;
    return stream;
}

int krc16_fclose(krc16_file* stream) {
    if (stream == 0 || !stream->in_use || stream == &console_out ||
        stream == &console_err) {
        return -1;
    }
    int result = krc16_sys_close(stream->descriptor);
    stream->in_use = 0;
    stream->descriptor = 0;
    stream->writable = 0;
    stream->error = 0;
    stream->end_of_file = 0;
    stream->pushback = -1;
    return result < 0 ? -1 : 0;
}

unsigned krc16_fread(void* buffer, unsigned size, unsigned count,
                     krc16_file* stream) {
    if (stream == 0 || !stream->in_use || size == 0 || count == 0) {
        return 0;
    }
    unsigned char* destination = (unsigned char*)buffer;
    unsigned total = size * count;
    unsigned filled = 0;
    /* The pushback slot holds one byte that was read ahead. */
    if (stream->pushback >= 0) {
        destination[filled++] = (unsigned char)stream->pushback;
        stream->pushback = -1;
    }
    while (filled < total) {
        unsigned chunk = total - filled;
        if (chunk > 4096u) {
            chunk = 4096u;
        }
        int read_count = krc16_sys_read(stream->descriptor, destination + filled, chunk);
        if (read_count < 0) {
            stream->error = 1;
            break;
        }
        if (read_count == 0) {
            stream->end_of_file = 1;
            break;
        }
        filled += (unsigned)read_count;
        stream->position += (long)read_count;
    }
    return filled / size;
}

unsigned krc16_fwrite(const void* buffer, unsigned size, unsigned count,
                      krc16_file* stream) {
    if (stream == 0 || !stream->in_use || size == 0 || count == 0) {
        return 0;
    }
    const unsigned char* source = (const unsigned char*)buffer;
    unsigned total = size * count;
    unsigned written = 0;
    while (written < total) {
        unsigned chunk = total - written;
        if (chunk > 4096u) {
            chunk = 4096u;
        }
        int write_count =
            krc16_sys_write(stream->descriptor, source + written, chunk);
        if (write_count <= 0) {
            stream->error = 1;
            break;
        }
        written += (unsigned)write_count;
        stream->position += (long)write_count;
    }
    return written / size;
}

int krc16_fgetc(krc16_file* stream) {
    if (stream == 0 || !stream->in_use) {
        return -1;
    }
    if (stream->pushback >= 0) {
        int value = stream->pushback;
        stream->pushback = -1;
        stream->position += 1;
        return value;
    }
    unsigned char byte = 0;
    int read_count = krc16_sys_read(stream->descriptor, &byte, 1);
    if (read_count <= 0) {
        if (read_count < 0) {
            stream->error = 1;
        } else {
            stream->end_of_file = 1;
        }
        return -1;
    }
    stream->position += 1;
    return (int)byte;
}

int krc16_ungetc(int character, krc16_file* stream) {
    if (stream == 0 || !stream->in_use || character < 0) {
        return -1;
    }
    stream->pushback = character;
    stream->end_of_file = 0;
    stream->position -= 1;
    return character;
}

int krc16_fputc(int character, krc16_file* stream) {
    if (stream == 0 || !stream->in_use) {
        return -1;
    }
    unsigned char byte = (unsigned char)character;
    int write_count = krc16_sys_write(stream->descriptor, &byte, 1);
    if (write_count <= 0) {
        stream->error = 1;
        return -1;
    }
    stream->position += 1;
    return (int)byte;
}

char* krc16_fgets(char* buffer, int capacity, krc16_file* stream) {
    if (buffer == 0 || capacity <= 0 || stream == 0) {
        return 0;
    }
    int written = 0;
    while (written < capacity - 1) {
        int character = krc16_fgetc(stream);
        if (character < 0) {
            break;
        }
        buffer[written++] = (char)character;
        if (character == '\n') {
            break;
        }
    }
    if (written == 0) {
        return 0;
    }
    buffer[written] = 0;
    return buffer;
}

int krc16_fputs(const char* text, krc16_file* stream) {
    if (text == 0) {
        return -1;
    }
    int written = 0;
    while (text[written] != 0) {
        if (krc16_fputc(text[written], stream) < 0) {
            return -1;
        }
        written++;
    }
    return written;
}

long krc16_ftell(krc16_file* stream) {
    if (stream == 0 || !stream->in_use) {
        return -1;
    }
    return stream->position;
}

int krc16_fseek(krc16_file* stream, long offset, int origin) {
    if (stream == 0 || !stream->in_use) {
        return -1;
    }
    long target = offset;
    if (origin == KRC16_SEEK_CUR) {
        target = stream->position + offset;
    } else if (origin != KRC16_SEEK_SET) {
        /* The host offers an absolute seek, and a seek from the end would need
           the file size first, so the end origin is not supported. */
        return -1;
    }
    if (target < 0) {
        return -1;
    }
    long position = krc16_sys_seek(stream->descriptor, target);
    if (position < 0) {
        return -1;
    }
    stream->pushback = -1;
    stream->end_of_file = 0;
    stream->position = position;
    return 0;
}

int krc16_feof(krc16_file* stream) {
    return stream != 0 && stream->end_of_file != 0 ? 1 : 0;
}

int krc16_ferror(krc16_file* stream) {
    return stream != 0 && stream->error != 0 ? 1 : 0;
}

void krc16_clearerr(krc16_file* stream) {
    if (stream != 0) {
        stream->error = 0;
        stream->end_of_file = 0;
    }
}

int krc16_remove(const char* path) {
    return krc16_sys_unlink(path) < 0 ? -1 : 0;
}

int krc16_rename(const char* from, const char* to) {
    return krc16_sys_rename(from, to) < 0 ? -1 : 0;
}
