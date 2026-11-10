class Value {
public:
    Value(int value) { value_ = value; }
    int operator-() const { return value_; }

private:
    int value_;
};

int main() {
    Value value(4);
    return -value;
}
