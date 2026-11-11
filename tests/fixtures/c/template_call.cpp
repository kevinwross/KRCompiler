template <typename T>
T maximum(T left, T right) {
    return left > right ? left : right;
}

int main() {
    return maximum<int>(3, 4);
}
