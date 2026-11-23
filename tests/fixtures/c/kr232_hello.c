int putchar_(int ch);

/* No string literal: a literal's address is a global relocation this backend does
   not emit yet, and the check is that the console receives bytes. */
int main(void) {
    putchar_(72);
    putchar_(105);
    putchar_(33);
    putchar_(10);
    return 0;
}
