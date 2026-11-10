class Counter {
public:
    Counter(int value) { value_ = value; }
    void operator+=(const Counter& other) { value_ = value_ + other.value_; }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    Counter total(3);
    Counter increment(4);
    total += increment;
    return total.get();
}
