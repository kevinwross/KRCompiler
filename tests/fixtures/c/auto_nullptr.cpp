int main() {
    auto x = 3.5;
    double y = x + 0.25;
    int* pointer = nullptr;
    int extra = (pointer == nullptr) ? 1 : 0;
    return (int)y + extra;
}
