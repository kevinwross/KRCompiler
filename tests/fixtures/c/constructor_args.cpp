class Pair {
public:
    int a;
    int b;
    Pair(int x, int y) { a = x; b = y; }
};

int main() {
    Pair pair(3, 4);
    return pair.a + pair.b;
}
