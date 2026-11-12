#include <string.h>
#include <time.h>

int main(void) {
    time_t epoch = 0;
    struct tm *value = gmtime(&epoch);
    if (value == 0) {
        return 1;
    }
    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%Z %z", value) == 0 ||
        strcmp(buffer, "UTC +0000") != 0) {
        return 2;
    }
    return 0;
}
