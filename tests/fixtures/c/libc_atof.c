int printf(const char *format, ...);
double atof(const char *text);
float strtof(const char *text, char **end);
long long llabs(long long value);

int main(void) {
    double value = atof("2.5");
    float single = strtof("1.25", 0);
    long long magnitude = llabs(-7);
    printf("%.2f %.2f %lld\n", value, (double)single, magnitude);
    return value == 2.5 && single == 1.25f && magnitude == 7 ? 0 : 1;
}
