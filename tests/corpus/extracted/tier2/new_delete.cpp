// Tier 2 — new and delete. A single object, an array, a class with a destructor
// reached through delete, and the round trip of a value stored through a pointer.
//
// KR32-only: Tier 2 is the C++ core and the plan's tier table assigns it to KR32.

int destroyed = 0;

class Guard {
public:
    Guard() : value_(0) { }
    ~Guard() { destroyed = destroyed + 1; }
    void set(int value) { value_ = value; }
    int get() const { return value_; }
private:
    int value_;
};

int main() {
    int* single = new int;
    *single = 42;
    if (*single != 42) { return 1; }
    delete single;

    int* array = new int[4];
    for (int index = 0; index < 4; index = index + 1) {
        array[index] = index * 3;
    }
    if (array[0] != 0) { return 2; }
    if (array[3] != 9) { return 3; }
    delete[] array;

    // An object allocated with new is constructed, and delete runs its destructor.
    destroyed = 0;
    Guard* guard = new Guard;
    guard->set(7);
    if (guard->get() != 7) { return 4; }
    delete guard;
    if (destroyed != 1) { return 5; }

    // An object allocated with new[] and released with delete[] constructs and
    // destroys each element.
    destroyed = 0;
    Guard* guards = new Guard[3];
    guards[0].set(1);
    guards[1].set(2);
    guards[2].set(3);
    if (guards[0].get() + guards[1].get() + guards[2].get() != 6) { return 6; }
    delete[] guards;
    if (destroyed != 3) { return 7; }

    // A pointer to a member of a heap array, written through and read back.
    int* values = new int[2];
    int* second = values + 1;
    *second = 11;
    if (values[1] != 11) { return 8; }
    delete[] values;

    return 0;
}
