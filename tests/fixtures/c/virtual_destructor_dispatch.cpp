int printf(const char *format, ...);

struct Base {
    virtual void report() {
        printf("base f\n");
    }

    virtual ~Base() {
        report();
    }
};

struct Derived : Base {
    void report() override {
        printf("derived f\n");
    }
};

int main() {
    Base *object = new Derived();
    delete object;
    return 0;
}
