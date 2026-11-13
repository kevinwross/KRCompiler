int main(void) {
    int *pointer = 0;
    int value = 1;
    if (!pointer) { pointer = &value; }
    return *pointer;
}
