#include <string.h>

int main(void) {
    const char text[] = "hello world";
    const char *found = (const char *)memmem(text, 11, "world", 5);
    if (found == 0 || found[0] != 'w') {
        return 1;
    }
    if (memmem(text, 11, "xxxxx", 5) != 0) {
        return 2;
    }
    if (memmem(text, 11, "", 0) == 0) {
        return 3;
    }
    return 0;
}
