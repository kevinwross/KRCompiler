class Value {
public:
    Value() { value_ = 1; }
    Value(const Value& other) { value_ = other.value_ + 1; }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    Value first;
    Value second = first;
    return second.get();
}
