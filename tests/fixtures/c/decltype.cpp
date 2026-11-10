int main() {
    auto value = 3.5;
    decltype(value) copy = value + 0.5;
    return (int)copy;
}
