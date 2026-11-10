class Counter {
public:
    Counter() { value_ = 3; }
    int get() { return value_; }
private:
    int value_;
};

int read_counter() {
    static Counter counter;
    return counter.get();
}

int main() {
    return read_counter();
}
