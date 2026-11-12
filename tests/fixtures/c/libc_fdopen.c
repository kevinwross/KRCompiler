#include <stdio.h>
#include <unistd.h>

int main(void) {
    int descriptor = open("kr32_fdopen_test.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (descriptor < 0) {
        return 1;
    }
    FILE *stream = fdopen(descriptor, "w");
    if (stream == 0 || fileno(stream) != descriptor) {
        return 2;
    }
    if (fputs("fd", stream) != 0) {
        return 3;
    }
    fclose(stream);

    descriptor = open("kr32_fdopen_test.txt", O_RDONLY);
    if (descriptor < 0) {
        return 4;
    }
    stream = fdopen(descriptor, "r");
    if (stream == 0) {
        return 5;
    }
    char buffer[4];
    if (fgets(buffer, sizeof(buffer), stream) == 0) {
        return 6;
    }
    fclose(stream);
    unlink("kr32_fdopen_test.txt");
    if (buffer[0] != 'f' || buffer[1] != 'd') {
        return 7;
    }
    return 0;
}
