struct S {
    int value;

    S(int input) : value(input) {}

    bool operator<(S const &other) const;
};

bool S::operator<(S const &other) const { return value < other.value; }

int main() {
    S a(1);
    S b(2);
    return a < b;
}
