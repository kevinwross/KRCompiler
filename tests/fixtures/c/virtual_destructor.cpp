int printf(const char *format, ...);

struct Base {
    virtual ~Base() {
        printf("base\n");
    }
};

struct Derived : Base {
    ~Derived() {
        printf("derived\n");
    }
};

int main() {
    Base *object = new Derived();
    delete object;
    return 0;
}
