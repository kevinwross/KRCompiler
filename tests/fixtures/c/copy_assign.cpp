class Value {
public:
    Value() { value_ = 1; }
    Value& operator=(const Value& other) {
        value_ = other.value_ + 2;
        return *this;
    }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    Value first;
    Value second;
    Value third;
    third = second = first;
    return third.get();
}
