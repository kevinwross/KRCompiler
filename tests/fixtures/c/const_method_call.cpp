struct S {
    int value;
    S() : value(1) {}
    int get() const { return value; }
    void set(int next) { value = next; }
};

int main() {
    const S constant;
    S changeable;
    changeable.set(2);
    int read = constant.get();
    return read;
}
