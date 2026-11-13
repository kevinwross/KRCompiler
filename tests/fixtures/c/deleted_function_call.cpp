// A deleted member function may be declared and never defined, but naming it
// is ill-formed. The compiler must reject the call below.
struct S {
    void keep() {}
    void drop() = delete;
};

int main() {
    S value;
    value.keep();
    value.drop();
    return 0;
}
