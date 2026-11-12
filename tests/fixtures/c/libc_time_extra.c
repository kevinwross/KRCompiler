#include <string.h>
#include <time.h>
int main(void) {
    time_t epoch = 0;
    struct tm *value = gmtime(&epoch);
    if (value == 0) return 1;
    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%C %D %h%n%t", value) == 0) return 2;
    if (strcmp(buffer, "19 01/01/70 Jan\n\t") != 0) return 3;
    return 0;
}
