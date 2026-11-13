struct S {
    int value;
    S() : value(1) {}
};

int main() {
    const S constant;
    S changeable;
    changeable.value = 2;
    constant.value = 3;
    return constant.value;
}
