#include <stdio.h>
#include <string.h>

int main(void) {
    FILE *stream = fopen("kr32_stdio_test.txt", "w");
    if (stream == 0) {
        return 1;
    }
    if (fwrite("abc", 1, 3, stream) != 3) {
        return 2;
    }
    if (fclose(stream) != 0) {
        return 3;
    }
    stream = fopen("kr32_stdio_test.txt", "r");
    if (stream == 0) {
        return 4;
    }
    char buffer[4];
    if (fread(buffer, 1, 3, stream) != 3) {
        return 5;
    }
    buffer[3] = 0;
    if (fclose(stream) != 0) {
        return 6;
    }
    if (remove("kr32_stdio_test.txt") != 0) {
        return 7;
    }
    return strcmp(buffer, "abc");
}
