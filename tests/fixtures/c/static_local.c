int counter(void) {
    static int value = 0;
    value = value + 1;
    return value;
}
int main(void) {
    return counter() + counter();
}
