struct S {
    static const int value = 7;
};

struct T {
    inline static int value = 5;
};

int main() {
    return S::value + T::value;
}
