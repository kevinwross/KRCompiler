int main() {
    int base = 3;
    auto add = [&](int value) { base = base + value; };
    add(4);
    return base;
}
