/* Prints through the installed package, which is what a program that includes
   <kr232/cons.h> does: the header declares cons_puts and the staged package
   object defines it. Linking only the startup leaves the call undefined. */
#include <kr232/cons.h>

int main(void) {
    cons_puts("packages\n");
    return 0;
}
