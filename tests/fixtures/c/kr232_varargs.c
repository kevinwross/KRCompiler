/* A printf-style call: the variadic callee reads a mixed integer and double
   argument block from what va_start hands it. Each argument occupies an
   eight-byte stack slot, which is the kr232 variadic convention. */
int mixed(int count, ...) {
    char* ap = __builtin_va_start();
    unsigned* first = (unsigned*)ap;
    double* second = (double*)(ap + 8);
    return (int)*first + (int)(*second * 2.0);
}

int main(void) {
    return mixed(2, 40, 2.5);   /* 40 + 5 = 45 */
}
