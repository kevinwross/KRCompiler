// `(*this)[index]` and `(*pointer)[index]` name the object the operand refers
// to, so a record's operator[] receives that address instead of a loaded copy.
// Returns 42 when subscripting through `*this` reads and writes the object.

struct Counter {
    int values[4];
    int count;

    Counter() : count(0) {}

    // operator[] is reached through `*this`, both to write and to read.
    int& operator[](int index) { return values[index]; }

    void add(int value) {
        if (count < 4) {
            (*this)[count] = value;
            count = count + 1;
        }
    }

    int sum() {
        int total = 0;
        int index = 0;
        while (index < count) {
            total = total + (*this)[index];
            index = index + 1;
        }
        return total;
    }
};

int main() {
    Counter counter;
    counter.add(7);
    counter.add(35);
    if (counter[0] != 7 || counter[1] != 35) {
        return 1;
    }
    if (counter.count != 2) {
        return 2;
    }
    if (counter.sum() != 42) {
        return 3;
    }

    // Through a pointer the same path applies.
    Counter* pointer = &counter;
    (*pointer)[2] = 5;
    if (counter[2] != 5) {
        return 4;
    }
    if (counter.sum() != 42) {
        return 5;
    }
    return 42;
}
