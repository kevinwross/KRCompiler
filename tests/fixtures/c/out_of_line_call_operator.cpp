struct S {
    int operator()(int input);
};

int S::operator()(int input) { return input + 1; }

int main() {
    S s;
    return s(2);
}
