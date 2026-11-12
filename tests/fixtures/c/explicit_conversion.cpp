struct S {
    int value;

    S(int input) : value(input) {}

    explicit operator bool() const { return value != 0; }
};

int main() {
    S s(1);
    if (s) {
        return 0;
    }
    return 1;
}
