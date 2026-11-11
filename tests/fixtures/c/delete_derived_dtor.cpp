int puts(const char *text);

class Base {
public:
    ~Base() { puts("base"); }
};

class Derived : public Base {
public:
    ~Derived() { puts("derived"); }
};

int main() {
    Derived *object = new Derived();
    puts("main");
    delete object;
    return 0;
}
