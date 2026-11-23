struct triple { char a; char b; char c; };

class Animal {
public:
    virtual int legs() const { return 4; }
    virtual ~Animal() {}
};

class Bird : public Animal {
public:
    int legs() const override { return 2; }
};

int sum3(struct triple t) { return t.a + t.b + t.c; }
struct triple make(void) { struct triple t; t.a = 4; t.b = 5; t.c = 6; return t; }

int main() {
    Bird bird;
    Animal* animal = &bird;
    int legs = animal->legs();
    int total = sum3(make());
    int values[3];
    values[0] = legs;
    values[1] = total;
    values[2] = 0;
    return values[0] + values[1] + values[2];
}
