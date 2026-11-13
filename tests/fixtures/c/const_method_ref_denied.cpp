struct S {
    int value;
    S() : value(1) {}
    int get() const { return value; }
    void set(int next) { value = next; }
};

int main() {
    S changeable;
    const S &reference = changeable;
    int read = reference.get();
    reference.set(2);
    return read;
}
