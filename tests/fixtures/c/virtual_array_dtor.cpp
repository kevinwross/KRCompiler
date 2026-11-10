int destroyed = 0;

class Base {
public:
    virtual ~Base() {}
};

class Derived : public Base {
public:
    virtual ~Derived() { destroyed = destroyed + 1; }
};

int main() {
    Base* values = new Derived[2];
    delete[] values;
    return destroyed;
}
