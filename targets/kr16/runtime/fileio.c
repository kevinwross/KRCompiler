/* The KR16 standard stream functions over the host-service wrappers.
 *
 * `<stdio.h>` declares these for the KR16 profile. The wrappers in `host.s`
 * speak the VM's service block protocol; this file gives them a stream
 * interface, so a KR16 program uses `fopen`/`fread`/`fwrite` like any other C
 * program. Descriptors 1 and 2 are the console, which the VM writes directly,
 * so a stream over them opens no file.
 */

#include <stdio.h>
#include <krc/kr16_file.h>

int krc16_sys_open(const char* path, unsigned flags);
int krc16_sys_close(int fd);
int krc16_sys_read(int fd, void* buffer, unsigned count);
int krc16_sys_write(int fd, const void* buffer, unsigned count);
long krc16_sys_seek(int fd, long offset);
int krc16_sys_unlink(const char* path);
int krc16_sys_rename(const char* from, const char* to);

#define KRC16_STREAM_COUNT 16

static struct krc16_file streams[KRC16_STREAM_COUNT];

/* The console streams are initialised on first use: a static record in the
   image is zero-filled, while a pointer initialiser would need a relocation
   this profile does not emit. */
static struct krc16_file console_out;
static struct krc16_file console_err;

static void open_console(struct krc16_file* stream, int descriptor) {
    if (!stream->in_use) {
        stream->in_use = 1;
        stream->descriptor = descriptor;
        stream->writable = 1;
        stream->pushback = -1;
        stream->position = 0;
    }
}

FILE* __krc16_stdout(void) {
    open_console(&console_out, 1);
    return &console_out;
}

FILE* __krc16_stderr(void) {
    open_console(&console_err, 2);
    return &console_err;
}

/* Translates a mode string into the VM's open flags: read, write with create
 * and truncate, append with create, and any of them with `+` for read-write. */
static unsigned mode_flags(const char* mode) {
    if (mode == 0 || mode[0] == 0) {
        return KRC16_OPEN_READ;
    }
    unsigned flags = 0;
    int index = 0;
    while (mode[index] != 0) {
        if (mode[index] == '+') {
            flags = KRC16_OPEN_READ_WRITE;
        } else if (mode[index] == 'w') {
            flags = KRC16_OPEN_WRITE | KRC16_OPEN_CREATE | KRC16_OPEN_TRUNCATE;
        } else if (mode[index] == 'a') {
            flags = KRC16_OPEN_WRITE | KRC16_OPEN_CREATE | KRC16_OPEN_APPEND;
        }
        index++;
    }
    return flags;
}

static FILE* allocate_stream(void) {
    /* Walk with a pointer: the KR16 backend scales a struct step but not a
       dynamic index into an array of structs. */
    struct krc16_file* stream = streams;
    int index = 0;
    while (index < KRC16_STREAM_COUNT) {
        if (!stream->in_use) {
            stream->in_use = 1;
            stream->error = 0;
            stream->end_of_file = 0;
            stream->pushback = -1;
            stream->position = 0;
            return stream;
        }
        stream++;
        index++;
    }
    return 0;
}

FILE* fopen(const char* path, const char* mode) {
    const unsigned flags = mode_flags(mode);
    int descriptor = krc16_sys_open(path, flags);
    if (descriptor < 0) {
        return 0;
    }
    FILE* stream = allocate_stream();
    if (stream == 0) {
        krc16_sys_close(descriptor);
        return 0;
    }
    stream->descriptor = descriptor;
    stream->writable = (flags & 1u) != 0u;
    return stream;
}

int fclose(FILE* stream) {
    if (stream == 0 || !stream->in_use || stream == __krc16_stdout() ||
        stream == __krc16_stderr()) {
        return EOF;
    }
    int result = krc16_sys_close(stream->descriptor);
    stream->in_use = 0;
    stream->descriptor = 0;
    stream->writable = 0;
    stream->error = 0;
    stream->end_of_file = 0;
    stream->pushback = -1;
    stream->position = 0;
    return result < 0 ? EOF : 0;
}

size_t fread(void* buffer, size_t size, size_t count, FILE* stream) {
    if (stream == 0 || !stream->in_use || size == 0 || count == 0) {
        return 0;
    }
    unsigned char* destination = (unsigned char*)buffer;
    const size_t total = size * count;
    size_t filled = 0;
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
        int read_count =
            krc16_sys_read(stream->descriptor, destination + filled, chunk);
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

size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream) {
    if (stream == 0 || !stream->in_use || size == 0 || count == 0) {
        return 0;
    }
    const unsigned char* source = (const unsigned char*)buffer;
    const size_t total = size * count;
    size_t written = 0;
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

int fgetc(FILE* stream) {
    if (stream == 0 || !stream->in_use) {
        return EOF;
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
        return EOF;
    }
    stream->position += 1;
    return (int)byte;
}

int ungetc(int character, FILE* stream) {
    if (stream == 0 || !stream->in_use || character < 0) {
        return EOF;
    }
    stream->pushback = character;
    stream->end_of_file = 0;
    stream->position -= 1;
    return character;
}

int fputc(int character, FILE* stream) {
    if (stream == 0 || !stream->in_use) {
        return EOF;
    }
    unsigned char byte = (unsigned char)character;
    int write_count = krc16_sys_write(stream->descriptor, &byte, 1);
    if (write_count <= 0) {
        stream->error = 1;
        return EOF;
    }
    stream->position += 1;
    return (int)byte;
}

int putc(int character, FILE* stream) { return fputc(character, stream); }

char* fgets(char* buffer, int capacity, FILE* stream) {
    if (buffer == 0 || capacity <= 0 || stream == 0) {
        return 0;
    }
    int written = 0;
    while (written < capacity - 1) {
        int character = fgetc(stream);
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

int fputs(const char* text, FILE* stream) {
    if (text == 0) {
        return EOF;
    }
    int written = 0;
    while (text[written] != 0) {
        if (fputc(text[written], stream) < 0) {
            return EOF;
        }
        written++;
    }
    return written;
}

long ftell(FILE* stream) {
    if (stream == 0 || !stream->in_use) {
        return -1;
    }
    return stream->position;
}

int fseek(FILE* stream, long offset, int origin) {
    if (stream == 0 || !stream->in_use) {
        return -1;
    }
    long target = offset;
    if (origin == SEEK_CUR) {
        target = stream->position + offset;
    } else if (origin != SEEK_SET) {
        /* The host offers an absolute seek, and a seek from the end would need
           the file size first, so SEEK_END is not supported. */
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

int feof(FILE* stream) { return stream != 0 && stream->end_of_file != 0 ? 1 : 0; }

int ferror(FILE* stream) { return stream != 0 && stream->error != 0 ? 1 : 0; }

void clearerr(FILE* stream) {
    if (stream != 0) {
        stream->error = 0;
        stream->end_of_file = 0;
    }
}

int remove(const char* path) { return krc16_sys_unlink(path) < 0 ? -1 : 0; }

int rename(const char* from, const char* to) {
    return krc16_sys_rename(from, to) < 0 ? -1 : 0;
}
