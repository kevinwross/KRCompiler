/* A comparison and a branch: the true path returns 1, the false path falls
   through to 42, so the exit status names which path the machine took. */
int main(void) {
    int a = 20;
    if (a > 21) {
        return 1;
    }
    return 42;
}
