#include <string.h>
#include <time.h>

int main(void) {
    time_t epoch = 0;
    struct tm *value = gmtime(&epoch);
    if (value == 0) {
        return 1;
    }
    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%F %T", value) == 0 ||
        strcmp(buffer, "1970-01-01 00:00:00") != 0) {
        return 2;
    }
    if (strftime(buffer, sizeof(buffer), "%R", value) == 0 ||
        strcmp(buffer, "00:00") != 0) {
        return 3;
    }
    if (strftime(buffer, sizeof(buffer), "%x", value) == 0 ||
        strcmp(buffer, "01/01/70") != 0) {
        return 4;
    }
    if (strftime(buffer, sizeof(buffer), "%c", value) == 0 ||
        strcmp(buffer, "Thu Jan  1 00:00:00 1970") != 0) {
        return 5;
    }
    return 0;
}
