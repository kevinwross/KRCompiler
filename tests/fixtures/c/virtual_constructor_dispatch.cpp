int printf(const char *format, ...);

struct Base {
    Base() {
        printf("base ctor\n");
        report();
    }

    virtual void report() {
        printf("base f\n");
    }

    virtual ~Base() {}
};

struct Derived : Base {
    void report() override {
        printf("derived f\n");
    }
};

int main() {
    Derived object;
    return 0;
}
