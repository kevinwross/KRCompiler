/* The KR16 file layer: a stream writes a file, reopens it, reads it back, seeks
   within it, and reports end of file. Returns 42. */

#include <krc/kr16_file.h>

static char name[] = "krc16-file-layer.txt";
static char first[] = "alpha";
static char second[] = "beta";
static char buffer[16];

int main(void) {
    /* Write two strings, then read them back through the stream layer. */
    krc16_file* stream =
        krc16_fopen(name, KRC16_OPEN_WRITE | KRC16_OPEN_CREATE | KRC16_OPEN_TRUNCATE);
    if (stream == 0) {
        return 1;
    }
    if (krc16_fwrite(first, 1, 5, stream) != 5) {
        return 2;
    }
    if (krc16_fputs(second, stream) != 4) {
        return 3;
    }
    if (krc16_fclose(stream) != 0) {
        return 4;
    }

    stream = krc16_fopen(name, KRC16_OPEN_READ);
    if (stream == 0) {
        return 5;
    }
    if (krc16_fread(buffer, 1, 9, stream) != 9) {
        return 6;
    }
    if (buffer[0] != 'a' || buffer[4] != 'a' || buffer[5] != 'b' ||
        buffer[8] != 'a') {
        return 7;
    }

    /* The whole file has been consumed, so the stream reports end of file. */
    if (krc16_fgetc(stream) >= 0) {
        return 8;
    }
    if (!krc16_feof(stream) || krc16_ferror(stream)) {
        return 9;
    }

    /* Seek back to the start and read one byte, then rewind by ungetc. */
    if (krc16_fseek(stream, 0, KRC16_SEEK_SET) != 0) {
        return 10;
    }
    if (krc16_ftell(stream) != 0) {
        return 11;
    }
    if (krc16_fgetc(stream) != 'a') {
        return 12;
    }
    if (krc16_ftell(stream) != 1) {
        return 13;
    }
    if (krc16_ungetc('z', stream) != 'z') {
        return 14;
    }
    if (krc16_fgetc(stream) != 'z') {
        return 15;
    }
    if (krc16_fclose(stream) != 0) {
        return 16;
    }

    /* A close returns the slot, so a second open of the same file works. */
    stream = krc16_fopen(name, KRC16_OPEN_READ);
    if (stream == 0) {
        return 17;
    }
    if (krc16_fgets(buffer, 6, stream) == 0) {
        return 18;
    }
    if (buffer[4] != 'a' || buffer[5] != 0) {
        return 19;
    }
    if (krc16_fclose(stream) != 0) {
        return 20;
    }
    if (krc16_remove(name) != 0) {
        return 21;
    }
    return 42;
}
