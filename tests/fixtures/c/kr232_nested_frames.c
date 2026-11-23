/* Two frames on the stack at once: inner is called from main, so a debugger
   that walks EFP sees both, and the return address in the inner frame lands
   inside main. */
int inner(int x) {
    return x + 1;
}

int main(void) {
    return inner(3);
}
