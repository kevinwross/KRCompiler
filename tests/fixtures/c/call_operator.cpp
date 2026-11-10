class Adder {
public:
    Adder(int base) { base_ = base; }
    int operator()(int value) { return base_ + value; }

private:
    int base_;
};

int main() {
    Adder add(3);
    return add(4);
}
