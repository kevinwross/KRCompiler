inline int global_value = 3;

int main() {
    if constexpr (true) {
        return global_value;
    }
    return 0;
}
