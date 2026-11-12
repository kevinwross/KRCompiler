struct S {
    static int get() { return 7; }
};

int main() {
    return S::get();
}
