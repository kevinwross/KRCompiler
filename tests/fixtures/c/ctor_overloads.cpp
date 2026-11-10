class Value {
public:
    Value() { value_ = 1; }
    Value(int value) { value_ = value; }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    Value first;
    Value second(3);
    return first.get() + second.get();
}
