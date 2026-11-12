struct S {
    static int value;
};

int S::value = 7;

int main() {
    S::value = 9;
    return S::value;
}
