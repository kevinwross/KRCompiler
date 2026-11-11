#include <stdio.h>
#include <string.h>

int main(void) {
    FILE *stream = fopen("kr32_fputs_test.txt", "w");
    if (stream == 0) {
        return 1;
    }
    if (fputs("text", stream) != 0 || putc('!', stream) != '!') {
        return 2;
    }
    fclose(stream);

    stream = fopen("kr32_fputs_test.txt", "r");
    if (stream == 0) {
        return 3;
    }
    char buffer[8];
    if (fgets(buffer, sizeof(buffer), stream) == 0) {
        return 4;
    }
    fclose(stream);
    remove("kr32_fputs_test.txt");
    return strcmp(buffer, "text!");
}
