int add(int left, int right) {
    return left + right;
}

int main(void) {
    int (*operation)(int, int) = add;
    return operation(2, 3);
}
