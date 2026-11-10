class Number {
public:
    explicit Number(int value) { value_ = value; }
    int get() const noexcept { return value_; }

private:
    int value_;
};

int main() {
    Number number(7);
    return number.get();
}
