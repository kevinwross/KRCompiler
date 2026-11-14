/* The KR16 stream functions through the standard names: a program includes
   <stdio.h> and calls fopen/fwrite/fread/fclose, and the profile supplies them
   from the file layer. Returns 42. */

#include <stdio.h>

static char name[] = "kr16-stdio.txt";
static char first[] = "alpha";
static char second[] = "beta";
static char buffer[16];

int main(void) {
    FILE* stream = fopen(name, "w");
    if (stream == 0) {
        return 1;
    }
    if (fwrite(first, 1, 5, stream) != 5) {
        return 2;
    }
    if (fputs(second, stream) != 4) {
        return 3;
    }
    if (fclose(stream) != 0) {
        return 4;
    }

    stream = fopen(name, "r");
    if (stream == 0) {
        return 5;
    }
    if (fread(buffer, 1, 9, stream) != 9) {
        return 6;
    }
    if (buffer[0] != 'a' || buffer[4] != 'a' || buffer[5] != 'b' ||
        buffer[8] != 'a') {
        return 7;
    }
    if (fgetc(stream) >= 0) {
        return 8;
    }
    if (!feof(stream) || ferror(stream)) {
        return 9;
    }
    if (fseek(stream, 0, SEEK_SET) != 0) {
        return 10;
    }
    if (ftell(stream) != 0) {
        return 11;
    }
    if (fgetc(stream) != 'a') {
        return 12;
    }
    if (ftell(stream) != 1) {
        return 13;
    }
    if (ungetc('z', stream) != 'z') {
        return 14;
    }
    if (fgetc(stream) != 'z') {
        return 15;
    }
    if (fclose(stream) != 0) {
        return 16;
    }

    /* Append mode adds to the file rather than truncating it. */
    stream = fopen(name, "a");
    if (stream == 0) {
        return 17;
    }
    if (fputs("omega", stream) != 5) {
        return 18;
    }
    if (fclose(stream) != 0) {
        return 19;
    }
    stream = fopen(name, "r");
    if (stream == 0) {
        return 20;
    }
    if (fread(buffer, 1, 14, stream) != 14) {
        return 21;
    }
    if (buffer[9] != 'o' || buffer[13] != 'a') {
        return 22;
    }
    if (fclose(stream) != 0) {
        return 23;
    }

    /* The console streams are usable without opening a file. */
    if (fputc('K', stdout) != 'K') {
        return 24;
    }
    if (fclose(stdout) == 0) {
        return 25;
    }
    if (remove(name) != 0) {
        return 26;
    }
    return 42;
}
