#include <strings.h>

int main(void) {
    if (strcasecmp("Hello", "hello") != 0) {
        return 1;
    }
    if (strncasecmp("ABCx", "abcy", 3) != 0) {
        return 2;
    }
    if (strcasecmp("a", "b") >= 0) {
        return 3;
    }
    return 0;
}
