int printf(const char *format, ...);
double strtod(const char *text, char **end);

int main(void) {
    char *end = 0;
    double first = strtod("  -3.25e2tail", &end);
    double second = strtod("0.5", 0);
    printf("%.2f %.2f %s\n", first, second, end);
    return first == -325.0 && second == 0.5 && end && end[0] == 't' ? 0 : 1;
}
