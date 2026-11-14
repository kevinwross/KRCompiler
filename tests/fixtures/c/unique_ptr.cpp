// `std::unique_ptr` owns one object: move construction and assignment transfer
// it, `release` gives it up, `reset` destroys what it held, and the destructor
// deletes the object when the owner goes out of scope. Coping is deleted, so a
// copy is a compile error. Returns 42.

#include <memory>

static int destroyed;

struct Item {
    int value;
    Item(int start) : value(start) {}
    ~Item() { destroyed = destroyed + 1; }
    int read() const { return value; }
};

int main() {
    {
        std::unique_ptr<Item> owner(new Item(7));
        if (!owner) {
            return 1;
        }
        if (owner->read() != 7) {
            return 2;
        }
        owner->value = 9;
        if (owner->read() != 9) {
            return 3;
        }

        // A move transfers ownership and leaves the source empty.
        std::unique_ptr<Item> moved(
            static_cast<std::unique_ptr<Item>&&>(owner));
        if (owner) {
            return 4;
        }
        if (moved->read() != 9) {
            return 5;
        }
        if (destroyed != 0) {
            return 6;
        }
    }

    // Leaving the scope destroyed exactly one object, once.
    if (destroyed != 1) {
        return 7;
    }

    // release gives up ownership without destroying.
    std::unique_ptr<Item> second(new Item(3));
    if (second->read() != 3) {
        return 8;
    }
    Item* raw = second.release();
    if (second) {
        return 9;
    }
    if (raw->read() != 3) {
        return 10;
    }
    delete raw;
    if (destroyed != 2) {
        return 11;
    }

    // reset with no argument destroys what the owner held.
    std::unique_ptr<Item> third(new Item(5));
    third.reset();
    if (third) {
        return 12;
    }
    if (destroyed != 3) {
        return 13;
    }

    // reset with an argument replaces the object and destroys the old one.
    std::unique_ptr<Item> fourth(new Item(11));
    fourth.reset(new Item(12));
    if (fourth->read() != 12) {
        return 14;
    }
    if (destroyed != 4) {
        return 15;
    }

    // Two owners can exchange what they hold.
    std::unique_ptr<Item> fifth(new Item(21));
    std::unique_ptr<Item> sixth(new Item(22));
    fifth.swap(sixth);
    if (fifth->read() != 22 || sixth->read() != 21) {
        return 16;
    }
    return 42;
}
