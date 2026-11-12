#include <string.h>

int main(void) {
    char text[] = "a,b,c";
    char *cursor = text;
    if (strcmp(strsep(&cursor, ","), "a") != 0) {
        return 1;
    }
    if (strcmp(strsep(&cursor, ","), "b") != 0) {
        return 2;
    }
    if (strcmp(strsep(&cursor, ","), "c") != 0) {
        return 3;
    }
    if (strsep(&cursor, ",") != 0) {
        return 4;
    }
    return 0;
}
