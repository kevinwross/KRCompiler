#include <stdlib.h>

int main(void) {
    if (labs(-5) != 5) {
        return 1;
    }
    if (atoll("12") != 12) {
        return 2;
    }
    if (strtoll("34", 0, 10) != 34) {
        return 3;
    }
    if (strtoull("56", 0, 10) != 56) {
        return 4;
    }
    return 0;
}
