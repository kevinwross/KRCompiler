// A read through a const object is legal; only an assignment is rejected.
// main returns 7 when the read produces the member's value.
struct S {
    int value;
    S() : value(7) {}
};

int main() {
    const S constant;
    int read = constant.value;
    return read;
}
