int destroyed = 0;

class Tracker {
public:
    Tracker() { value_ = 3; }
    ~Tracker() { destroyed = destroyed + 1; }
    int get() { return value_; }
private:
    int value_;
};

Tracker global;

int main() {
    return global.get() + destroyed;
}
