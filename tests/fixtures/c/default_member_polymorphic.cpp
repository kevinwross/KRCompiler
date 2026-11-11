class Base {
public:
    virtual int get() { return 1; }
    int base_value = 3;
};

class Derived : public Base {
public:
    int value = 9;
};

int main() {
    Derived object;
    return object.value + object.base_value;
}
