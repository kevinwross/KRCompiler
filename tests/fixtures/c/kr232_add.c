/* A computed result: two locals added together, so the returned value proves the
   ALU path rather than a constant the front end folded. */
int main(void) {
    int a = 20;
    int b = 22;
    return a + b;
}
