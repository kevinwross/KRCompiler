class Base {
public:
    virtual int value() const { return 1; }
};

class Derived : public Base {
public:
    int value() const override { return 2; }
};

int main() {
    Derived derived;
    Base& base = derived;
    return base.value();
}
