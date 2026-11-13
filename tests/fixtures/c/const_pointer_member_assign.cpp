struct S { int value; S() : value(1) {} };
int main() {
    S changeable;
    const S *pointer = &changeable;
    pointer->value = 3;
    return 0;
}
