int printf(const char *format, ...);
double fabs(double value);
double floor(double value);
double ceil(double value);
double sqrt(double value);

int main(void) {
    double root = sqrt(16.0);
    double down = floor(-1.25);
    double up = ceil(1.25);
    double magnitude = fabs(-3.5);
    printf("%.1f %.1f %.1f %.1f\n", root, down, up, magnitude);
    return root == 4.0 && down == -2.0 && up == 2.0 && magnitude == 3.5
               ? 0
               : 1;
}
