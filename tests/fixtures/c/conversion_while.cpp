struct S {
    int value;
    S(int input) : value(input) {}
    operator bool() const { return value != 0; }
};

int main() {
    S s(1);
    int count = 0;
    while (s) {
        count++;
        break;
    }
    return count;
}
