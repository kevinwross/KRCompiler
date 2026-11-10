int destroyed = 0;

class Item {
public:
    Item() {}
    ~Item() { destroyed = destroyed + 1; }
};

int main() {
    Item* items = new Item[3];
    delete[] items;
    return destroyed;
}
