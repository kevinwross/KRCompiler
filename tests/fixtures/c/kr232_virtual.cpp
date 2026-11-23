class Animal {
public:
    virtual int legs() const { return 4; }
    virtual ~Animal() {}
};

class Bird : public Animal {
public:
    int legs() const override { return 2; }
};

int main() {
    Bird bird;
    Animal* animal = &bird;
    return animal->legs();
}
