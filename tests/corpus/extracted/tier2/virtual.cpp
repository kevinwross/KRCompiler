// Tier 2 — virtual dispatch. Two derived classes overriding one base method, called
// through a base pointer, so the call must reach the dynamic type.
//
// The static call is checked too: a base pointer must reach the base's version when
// the function is not virtual, which is what distinguishes dispatch from name
// lookup.

class Animal {
public:
    Animal() { }
    virtual ~Animal() { }

    virtual int legs() const {
        return 0;
    }

    int describe() const {
        return legs();
    }

    int always_four() const {
        return 4;
    }
};

class Bird : public Animal {
public:
    virtual int legs() const {
        return 2;
    }
};

class Dog : public Animal {
public:
    virtual int legs() const {
        return 4;
    }
};

int main() {
    Animal animal;
    Bird bird;
    Dog dog;

    // Direct calls reach each class's own version.
    if (animal.legs() != 0) { return 1; }
    if (bird.legs() != 2) { return 2; }
    if (dog.legs() != 4) { return 3; }

    // Through a base pointer: the dynamic type decides.
    Animal* pointer = &bird;
    if (pointer->legs() != 2) { return 4; }
    pointer = &dog;
    if (pointer->legs() != 4) { return 5; }
    pointer = &animal;
    if (pointer->legs() != 0) { return 6; }

    // A non-virtual member is inherited, not dispatched: the base's body runs and
    // reaches legs() virtually from there.
    pointer = &bird;
    if (pointer->always_four() != 4) { return 7; }
    if (pointer->describe() != 2) { return 8; }

    return 0;
}
