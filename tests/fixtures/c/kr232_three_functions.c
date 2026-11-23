/* Three functions in one translation unit where two call the same extern and the
   third calls a sibling. Every function once emitted its blocks as `.Lbb0`, so
   the assembler bound the sibling's loop branch to another function's label: the
   loop never ran and this returned -1 instead of 131. Labels are function-scoped
   now, and this row fails if that regresses. */
int stub6(int n, ...) {
    (void)n;
    return 1;
}

int counter;

int add_char(int c) {
    stub6(1, 0, 0, 0, 0, 0);
    counter = counter + c;
    return c;
}

int add_text(const char* t) {
    while (*t != 0) {
        add_char(*t);
        ++t;
    }
    return counter;
}

int unused_reader(void) {
    const int got = stub6(2, 0, 0, 0, 0, 0);
    if (got != 1) {
        return -1;
    }
    return got;
}

int main(void) {
    return add_text("AB");   /* 65 + 66 = 131 */
}
