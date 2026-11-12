#include <stdlib.h>
static int flag;
static void finish(void) { flag = 1; }
int main() { atexit(finish); _Exit(7); return 1; }
