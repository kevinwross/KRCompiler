constexpr int magnitude(int value) {
    if (value < 0) {
        return -value;
    }
    return value;
}

constexpr int count = magnitude(-3);

int main() {
    int values[count];
    values[2] = 7;
    return values[2];
}
