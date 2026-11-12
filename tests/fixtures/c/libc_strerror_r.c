#include <string.h>

int main(void) {
    char buffer[32];
    if (strerror_r(2, buffer, sizeof(buffer)) != 0) {
        return 1;
    }
    if (buffer[0] == 0) {
        return 2;
    }
    if (strerror_r(2, buffer, 0) == 0) {
        return 3;
    }
    return 0;
}
