class Value {
public:
    Value() { value_ = 1; }
    void operator=(const Value& other) { value_ = 2; }
    void operator=(Value&& other) { value_ = 3; }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    Value first;
    Value second;
    second = first;
    int copied = second.get();
    second = std::move(first);
    return copied * 10 + second.get();
}
