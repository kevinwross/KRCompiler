// A mutable member stays assignable through a const object; a plain member does
// not. main returns 5 when the mutable write lands.
struct S {
    int value;
    mutable int counter;
    S() : value(1), counter(0) {}
};

int main() {
    const S constant;
    constant.counter = 5;
    return constant.counter;
}
