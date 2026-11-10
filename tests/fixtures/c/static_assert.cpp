struct Pair {
    int first;
    int second;
};

static_assert(sizeof(Pair) == sizeof(int) * 2, "pair layout");

int main() {
    return 0;
}
