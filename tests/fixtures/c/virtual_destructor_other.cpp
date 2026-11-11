int printf(const char *format, ...);

struct Other {
    virtual int value() {
        return 7;
    }

    virtual ~Other() {}
};

Other *other_pointer;

struct Base {
    Base() {}

    virtual ~Base() {
        printf("value=%d\n", other_pointer->value());
    }
};

struct DerivedOther : Other {
    int value() override {
        return 9;
    }
};

struct Derived : Base {
    ~Derived() {}
};

int main() {
    DerivedOther other;
    other_pointer = &other;
    Derived *object = new Derived();
    delete object;
    return 0;
}
