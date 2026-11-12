struct S {
    int value;

    S(int input) : value(input) {}

    operator bool() const { return value != 0; }
};

int main() {
    S yes(1);
    S no(0);
    if (yes) {
        if (no) {
            return 1;
        }
        return 0;
    }
    return 1;
}
