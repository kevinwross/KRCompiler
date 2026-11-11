#include <string.h>
#include <time.h>

int main(void) {
    time_t epoch = 0;
    struct tm *value = gmtime(&epoch);
    if (value == 0 || value->tm_year != 70 || value->tm_mon != 0 ||
        value->tm_mday != 1 || value->tm_wday != 4) {
        return 1;
    }
    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", value) == 0 ||
        strcmp(buffer, "1970-01-01 00:00:00") != 0) {
        return 2;
    }
    if (mktime(value) != 0) {
        return 3;
    }
    if (localtime(&epoch) == 0) {
        return 4;
    }
    if (strcmp(asctime(value), "Thu Jan  1 00:00:00 1970\n") != 0) {
        return 5;
    }
    if (ctime(&epoch) == 0) {
        return 6;
    }
    return 0;
}
