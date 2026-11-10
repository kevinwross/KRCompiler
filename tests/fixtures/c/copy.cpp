class Pair {
public:
    int a;
    int b;
};

int main() {
    Pair first;
    first.a = 3;
    first.b = 4;
    Pair second = first;
    Pair third;
    third = second;
    return third.a + third.b;
}
