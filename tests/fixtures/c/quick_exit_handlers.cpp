#include <stdlib.h>
static int flag;
static void finish(void) { flag = 7; _Exit(flag); }
int main() { at_quick_exit(finish); quick_exit(0); return 1; }
