class Item {
public:
    Item() { value_ = 3; }
    int get() const { return value_; }

private:
    int value_;
};

int main() {
    alignas(16) char storage[sizeof(Item)];
    Item* item = new (storage) Item();
    return item->get();
}
