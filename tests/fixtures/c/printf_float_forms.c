#include <stdio.h>
#include <string.h>
int main() { char buffer[64]; snprintf(buffer, sizeof buffer, "%e", 1234.5); if (strcmp(buffer, "1.234500e+03") != 0) { return 1; } snprintf(buffer, sizeof buffer, "%g", 0.5); if (strcmp(buffer, "0.5") != 0) { return 2; } return 0; }
