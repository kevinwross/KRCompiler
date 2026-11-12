#include <locale.h>

int main(void) {
    if (setlocale(LC_ALL, "C") == 0) {
        return 1;
    }
    if (setlocale(LC_ALL, "not-a-locale") != 0) {
        return 2;
    }
    struct lconv *info = localeconv();
    if (info == 0 || info->decimal_point == 0 ||
        info->decimal_point[0] != '.') {
        return 3;
    }
    return 0;
}
