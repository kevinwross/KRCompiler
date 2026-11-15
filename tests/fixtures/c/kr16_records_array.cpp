// An array of records inside a record, on the sixteen-bit profile: writing an
// element goes through a pointer computed with a runtime index, and leaving the
// scope destroys every element. The element size decides the scale, so this uses
// a two-byte element. Returns 42.

static int destroyed;

struct Item {
    int value;
    Item() : value(0) {}
    ~Item() { destroyed = destroyed + 1; }
};

struct Holder {
    Item items[3];
};

int main() {
    {
        Holder holder;
        holder.items[0].value = 3;
        holder.items[1].value = 4;
        holder.items[2].value = 5;
        if (holder.items[0].value != 3 || holder.items[2].value != 5) {
            return 1;
        }
        if (destroyed != 0) {
            return 2;
        }
    }

    // Every element was destroyed exactly once.
    if (destroyed != 3) {
        return 3;
    }
    return 42;
}