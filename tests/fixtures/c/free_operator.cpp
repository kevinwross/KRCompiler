class Number {
public:
    int value;
    Number(int v) { value = v; }
};

int operator+(const Number& left, const Number& right) {
    return left.value + right.value;
}

int main() {
    Number first(3);
    Number second(4);
    return first + second;
}
