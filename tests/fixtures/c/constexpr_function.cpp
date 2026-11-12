constexpr int square(int value) { return value * value; }
static_assert(square(3) == 9, "square");
int main() { return square(3); }
