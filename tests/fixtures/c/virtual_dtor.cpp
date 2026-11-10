int destroyed = 0;

class Base {
public:
    virtual ~Base() {}
};

class Derived : public Base {
public:
    ~Derived() { destroyed = 11; }
};

int main() {
    Base* object = new Derived();
    delete object;
    return destroyed;
}
