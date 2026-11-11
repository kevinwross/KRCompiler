#ifndef KRC_TIME_H
#define KRC_TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000000

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t *result);
struct tm *gmtime(const time_t *value);
struct tm *localtime(const time_t *value);
time_t mktime(struct tm *value);
size_t strftime(char *buffer, size_t size, const char *format,
                const struct tm *value);
char *asctime(const struct tm *value);
char *ctime(const time_t *value);
double difftime(time_t end, time_t beginning);
clock_t clock(void);

#endif
