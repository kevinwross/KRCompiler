#include <stdio.h>

int main(void) {
    FILE *stream = fopen("kr32_fpos_test.txt", "w");
    if (stream == 0) {
        return 1;
    }
    fputs("abcdef", stream);
    fclose(stream);

    stream = fopen("kr32_fpos_test.txt", "r");
    if (stream == 0) {
        return 2;
    }
    setbuf(stream, 0);
    fpos_t start = 0;
    if (fgetpos(stream, &start) != 0 || start != 0) {
        return 3;
    }
    if (getc(stream) != 'a' || getc(stream) != 'b') {
        return 4;
    }
    if (fsetpos(stream, &start) != 0 || getc(stream) != 'a') {
        return 5;
    }
    stream = freopen("kr32_fpos_test.txt", "r", stream);
    if (stream == 0 || getc(stream) != 'a') {
        return 6;
    }
    fclose(stream);
    remove("kr32_fpos_test.txt");
    return 0;
}
