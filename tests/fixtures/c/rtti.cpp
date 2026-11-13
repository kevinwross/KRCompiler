// RTTI over the vtable/typeinfo layout in docs/abi.md: an object's vptr names
// its dynamic type_info one word before the first virtual slot, and each
// type_info names its base's, so `typeid` and `dynamic_cast` can answer from
// the object rather than from its static type. Returns 42 when every check
// holds.

#include <typeinfo>

struct Animal {
    virtual int legs() const { return 4; }
};

struct Bird : Animal {
    int legs() const override { return 2; }
};

struct Penguin : Bird {
    int legs() const override { return 2; }
};

int main() {
    Penguin penguin;
    Bird bird;
    Animal* zoo[2];
    zoo[0] = &penguin;
    zoo[1] = &bird;

    // typeid names the dynamic type through a base pointer.
    if (typeid(*zoo[0]) != typeid(penguin)) {
        return 1;
    }
    if (typeid(*zoo[1]) == typeid(penguin)) {
        return 2;
    }
    if (typeid(*zoo[0]) == typeid(bird)) {
        return 3;
    }

    // The name is the source-level class name, and a type_info reference
    // binds to the same object.
    if (typeid(*zoo[0]).name()[0] != 'P' || typeid(*zoo[0]).name()[6] != 'n') {
        return 4;
    }
    const std::type_info& info = typeid(*zoo[1]);
    if (info.name()[0] != 'B' || info != typeid(bird)) {
        return 5;
    }

    // A downcast through two levels, and the same object tested against the
    // wrong target.
    Penguin* first = dynamic_cast<Penguin*>(zoo[0]);
    if (first == 0 || first->legs() != 2) {
        return 6;
    }
    if (dynamic_cast<Penguin*>(zoo[1]) != 0) {
        return 7;
    }

    // An upcast is checked at compile time and always succeeds.
    Bird* up = dynamic_cast<Bird*>(zoo[0]);
    if (up == 0 || up->legs() != 2) {
        return 8;
    }
    if (dynamic_cast<Bird*>(zoo[0]) == 0) {
        return 9;
    }

    // A null source stays null.
    Animal* empty = 0;
    if (dynamic_cast<Penguin*>(empty) != 0) {
        return 10;
    }

    // The static form names a type rather than an object.
    if (typeid(Penguin) != typeid(*zoo[0])) {
        return 11;
    }

    return 42;
}
