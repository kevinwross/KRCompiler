class Value {
public:
    Value() { value_ = 1; }
    Value(Value&& other) { value_ = other.value_ + 3; }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    Value first;
    Value second((Value&&)first);
    return second.get();
}
