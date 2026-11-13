struct S {
    int value;
    S() : value(1) {}
    void set(int next) { value = next; }
};

int main() {
    const S constant;
    constant.set(2);
    return 0;
}
