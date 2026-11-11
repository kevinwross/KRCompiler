#include <stdio.h>
#include <string.h>

int main(void) {
    int number = 0;
    char word[8];
    char character = 0;
    if (sscanf("42 word Z", "%d %7s %c", &number, word, &character) != 3) {
        return 1;
    }
    if (number != 42 || strcmp(word, "word") != 0 || character != 'Z') {
        return 2;
    }
    int skipped = 0;
    if (sscanf("ignore 7", "%*s %d", &skipped) != 1 || skipped != 7) {
        return 3;
    }
    return 0;
}
