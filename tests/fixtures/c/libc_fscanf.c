#include <stdio.h>
#include <string.h>

int main(void) {
    FILE *stream = fopen("kr32_fscanf_test.txt", "w");
    if (stream == 0) {
        return 1;
    }
    fwrite("42 word", 1, 7, stream);
    fclose(stream);

    stream = fopen("kr32_fscanf_test.txt", "r");
    if (stream == 0) {
        return 2;
    }
    int number = 0;
    char word[8];
    if (fscanf(stream, "%d %7s", &number, word) != 2) {
        return 3;
    }
    fclose(stream);
    remove("kr32_fscanf_test.txt");
    if (number != 42 || strcmp(word, "word") != 0) {
        return 4;
    }
    return 0;
}
