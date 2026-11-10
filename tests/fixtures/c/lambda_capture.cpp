int main() {
    int base = 3;
    auto add = [base](int value) { return base + value; };
    return add(4);
}
