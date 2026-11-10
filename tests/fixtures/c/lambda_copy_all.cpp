int main() {
    int base = 3;
    int addend = 2;
    auto add = [=](int value) { return base + addend + value; };
    base = 10;
    return add(2);
}
