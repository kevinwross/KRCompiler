struct S {
    int value;
    S() = delete;
};
int main() {
    S object;
    return 0;
}
