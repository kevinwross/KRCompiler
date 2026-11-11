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
    Derived *objects = new Derived[2];
    puts("main");
    delete[] objects;
    return 0;
}
