constexpr int triple(int value) { return value * 3; }
constexpr int count = triple(2);

int main() {
    int values[count];
    values[5] = 7;
    return values[5];
}
