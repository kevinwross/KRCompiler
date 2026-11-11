#include <time.h>

int main(void) {
    time_t now = 0;
    if (time(&now) < 0 || now < 0) {
        return 1;
    }
    if (difftime(now + 10, now) != 10.0) {
        return 2;
    }
    if (clock() < 0) {
        return 3;
    }
    return 0;
}
