int add1(int value) {
    return value + 1;
}

int main(void) {
    int (*function)(int) = add1;
    return function(4);
}
