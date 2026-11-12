#include <stdlib.h>
#include <string.h>
int main() {
    if (getenv("KRC") != 0) return 1;
    if (setenv("KRC", "seven", 1) != 0) return 2;
    if (getenv("KRC") == 0 || strcmp(getenv("KRC"), "seven") != 0) return 3;
    if (setenv("KRC", "other", 0) != 0) return 4;
    if (strcmp(getenv("KRC"), "seven") != 0) return 5;
    if (setenv("KRC", "other", 1) != 0) return 6;
    if (strcmp(getenv("KRC"), "other") != 0) return 7;
    if (unsetenv("KRC") != 0) return 8;
    if (getenv("KRC") != 0) return 9;
    return 0;
}
