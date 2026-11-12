struct S {
    int value;

    S(int input) : value(input) {}

    S &operator++();
};

S &S::operator++() {
    value++;
    return *this;
}

int main() {
    S s(1);
    ++s;
    return s.value;
}
