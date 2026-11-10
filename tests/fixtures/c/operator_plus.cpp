class Number {
public:
    Number(int value) { value_ = value; }
    int operator+(const Number& other) { return value_ + other.value_; }

private:
    int value_;
};

int main() {
    Number first(3);
    Number second(4);
    return first + second;
}
