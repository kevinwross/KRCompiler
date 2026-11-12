#include <stdio.h>
int main() { char buffer[32]; snprintf(buffer, sizeof buffer, "%.2f", 3.5); return buffer[0] == '3' && buffer[1] == '.' && buffer[2] == '5' && buffer[3] == '0' ? 0 : 1; }
