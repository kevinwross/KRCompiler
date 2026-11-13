// A class with a constructor, a destructor, a virtual method, and a
// non-virtual method, plus a free function and a namespaced class, so the
// debug dump has one of each name shape (ADR 0030). The program returns 42 so
// the fixture can double as an executed test if one is added later.

namespace tools {

class Counter {
public:
    Counter() : total(0) {}
    void bump() { total = total + 1; }
    int value() const { return total; }

private:
    int total;
};

}  // namespace tools

struct Widget {
    Widget() : weight(0) {}
    ~Widget() { weight = 0; }
    virtual int size() const { return weight; }
    int doubled() const { return weight * 2; }
    int weight;
};

static int helper(int value) {
    return value + 1;
}

int main() {
    Widget widget;
    widget.weight = 7;
    tools::Counter counter;
    counter.bump();
    counter.bump();
    if (widget.size() != 7 || widget.doubled() != 14) {
        return 1;
    }
    if (counter.value() != 2) {
        return 2;
    }
    if (helper(41) != 42) {
        return 3;
    }
    return 42;
}
