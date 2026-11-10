struct Pair {
    int a;
    int b;
};

int main(void) {
    struct Pair pair;
    pair.a = 3;
    pair.b = 4;
    return pair.a + pair.b;
}
