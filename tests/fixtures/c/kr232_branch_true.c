/* The other side of the branch: 20 > 19 is true, so the early return runs. */
int main(void) {
    int a = 20;
    if (a > 19) {
        return 1;
    }
    return 42;
}
