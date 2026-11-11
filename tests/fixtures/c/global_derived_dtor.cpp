int puts(const char *text);

class Base {
public:
    ~Base() { puts("base"); }
};

class Derived : public Base {
public:
    ~Derived() { puts("derived"); }
};

Derived global;

int main() {
    puts("main");
    return 0;
}
