#include <math.h>

int main(void) {
    if (fmin(2.0, 7.0) != 2.0) {
        return 1;
    }
    if (fmax(2.0, 7.0) != 7.0) {
        return 2;
    }
    if (fmin(-3.0, -8.0) != -8.0) {
        return 3;
    }
    return 0;
}
