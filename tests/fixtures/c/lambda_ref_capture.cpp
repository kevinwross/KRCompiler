int main() {
    int base = 3;
    auto add = [&base](int value) { return base + value; };
    base = 4;
    return add(3);
}
