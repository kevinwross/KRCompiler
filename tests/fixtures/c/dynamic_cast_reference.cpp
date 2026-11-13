// A reference dynamic_cast cannot return null: a failed check throws
// std::bad_cast, which the caller catches like any other exception. Returns 42
// when the successful cast binds to the object and the failed one is caught by
// both std::bad_cast& and std::exception&.

#include <typeinfo>

struct Animal {
    virtual int legs() const { return 4; }
    int tag;
};

struct Bird : Animal {
    int legs() const override { return 2; }
};

struct Fish : Animal {
    int legs() const override { return 0; }
};

static int successful_cast(Animal& animal) {
    Bird& bird = dynamic_cast<Bird&>(animal);
    return bird.legs();
}

int main() {
    Bird bird;
    bird.tag = 7;
    Animal& base = bird;

    // A reference downcast to the dynamic type binds to the same object.
    Bird& down = dynamic_cast<Bird&>(base);
    if (down.legs() != 2 || down.tag != 7) {
        return 1;
    }
    if (successful_cast(base) != 2) {
        return 2;
    }

    // The same object tested against a sibling type throws bad_cast.
    Fish fish;
    Animal& other = fish;
    int caught = 0;
    try {
        Bird& wrong = dynamic_cast<Bird&>(other);
        (void)wrong;
    } catch (std::bad_cast& failure) {
        caught = failure.what()[0] == 's' ? 1 : 2;
    }
    if (caught != 1) {
        return 3;
    }

    // bad_cast derives from std::exception, so a base handler catches it.
    int from_base = 0;
    try {
        (void)dynamic_cast<Bird&>(other);
    } catch (std::exception& failure) {
        from_base = failure.what()[0] == 's' ? 1 : 2;
    }
    if (from_base != 1) {
        return 4;
    }

    return 42;
}
