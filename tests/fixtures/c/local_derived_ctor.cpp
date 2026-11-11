class Base {
public:
    Base() { value = 5; }
    int value;
};

class Derived : public Base {
};

int main() {
    Derived object;
    return object.value;
}
