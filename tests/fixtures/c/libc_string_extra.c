#include <stdlib.h>
#include <string.h>

int main(void) {
    char text[] = "a,b";
    char *save = 0;
    if (strcmp(strtok_r(text, ",", &save), "a") != 0) {
        return 1;
    }
    if (strcmp(strtok_r(0, ",", &save), "b") != 0) {
        return 2;
    }
    char transformed[4];
    if (strxfrm(transformed, "abc", 4) != 3 ||
        strcmp(transformed, "abc") != 0) {
        return 3;
    }
    if (strcoll("same", "same") != 0) {
        return 4;
    }
    char *copy = strndup("hello", 3);
    if (copy == 0 || strcmp(copy, "hel") != 0) {
        return 5;
    }
    free(copy);
    char memory[4];
    char *after = memccpy(memory, "abcd", 'c', 4);
    if (after != memory + 3 || memory[2] != 'c') {
        return 6;
    }
    return 0;
}
