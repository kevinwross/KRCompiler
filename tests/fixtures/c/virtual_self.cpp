class Base {
public:
    virtual int value() const { return 1; }
    int twice() const { return value() * 2; }
};

class Derived : public Base {
public:
    int value() const override { return 2; }
};

int main() {
    Derived derived;
    Base* base = &derived;
    return base->twice();
}
