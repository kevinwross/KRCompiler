struct Pair { int first; int second; };

int main() {
    struct Pair pair = {3, 4};
    auto [left, right] = pair;
    return left + right;
}
