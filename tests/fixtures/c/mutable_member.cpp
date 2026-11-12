struct S {
    mutable int value;

    S() : value(1) {}

    void inc() const { value++; }
};

int main() {
    S s;
    s.inc();
    return s.value;
}
