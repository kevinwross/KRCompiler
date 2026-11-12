struct S {
    int value;

    S() : S(7) {}
    S(int input) : value(input) {}
};

int main() {
    S s;
    return s.value;
}
