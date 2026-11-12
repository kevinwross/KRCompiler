#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *stream = fopen("kr32_getdelim_test.txt", "w");
    if (stream == 0) {
        return 1;
    }
    fputs("a;b", stream);
    fclose(stream);

    stream = fopen("kr32_getdelim_test.txt", "r");
    if (stream == 0) {
        return 2;
    }
    char *line = 0;
    size_t capacity = 0;
    const int first = getdelim(&line, &capacity, ';', stream);
    if (first != 2 || strcmp(line, "a;") != 0) {
        return 3;
    }
    const int second = getdelim(&line, &capacity, ';', stream);
    if (second != 1 || strcmp(line, "b") != 0) {
        return 4;
    }
    free(line);
    fclose(stream);
    remove("kr32_getdelim_test.txt");
    return 0;
}
