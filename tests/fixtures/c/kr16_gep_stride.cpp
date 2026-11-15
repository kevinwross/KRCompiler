// A runtime index into an array of records whose element is neither one, two,
// four, nor eight bytes, on the sixteen-bit profile. Three ints make a six-byte
// element, so the backend multiplies rather than shifting. Returns 42.

struct Item {
    int first;
    int second;
    int third;
};

struct Holder {
    Item items[3];
};

int main() {
    Holder holder;
    for (int index = 0; index < 3; index = index + 1) {
        holder.items[index].first = index + 1;
        holder.items[index].second = index + 20;
        holder.items[index].third = index + 40;
    }

    // Each element holds its own values, which only happens if the stride is six.
    if (holder.items[0].first != 1 || holder.items[0].third != 40) {
        return 1;
    }
    if (holder.items[1].first != 2 || holder.items[1].second != 21) {
        return 2;
    }
    if (holder.items[2].first != 3 || holder.items[2].third != 42) {
        return 3;
    }
    return 42;
}