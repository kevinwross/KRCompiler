#include <stdlib.h>
static int flag;
static void finish(void) { flag = 7; exit(flag); }
int main() { atexit(finish); exit(0); }
