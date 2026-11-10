class Defaults {
public:
    Defaults() {}
    int get() { return value; }
private:
    int value = 5;
};

int main() {
    Defaults object;
    return object.get();
}
