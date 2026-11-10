class Counter {
public:
    Counter(int value) { value_ = value; }
    int operator++() {
        value_ = value_ + 1;
        return value_;
    }
    int operator++(int) {
        int old = value_;
        value_ = value_ + 1;
        return old;
    }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    Counter counter(3);
    ++counter;
    int old = counter++;
    return old * 10 + counter.get();
}
