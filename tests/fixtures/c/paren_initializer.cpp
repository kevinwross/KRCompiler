// A parenthesized initializer declares a variable, not a function: the
// parenthesis is an initializer whenever its first token cannot begin a
// parameter declaration, and a class-typed name may also take a bare name as
// its constructor argument. Returns 42 when every form initializes.

struct Item {
    int value;
    Item() : value(0) {}
    Item(int start) : value(start) {}
    int read() const { return value; }
};

// The canonical owner form: a class template constructed from `new`.
template <typename T>
class Owner {
public:
    Owner() : pointer(0) {}
    explicit Owner(T* value) : pointer(value) {}
    T* get() const { return pointer; }

private:
    T* pointer;
};

int add(int left, int right);

int main() {
    // A builtin initialized from a literal in parentheses.
    int whole(7);
    if (whole != 7) {
        return 1;
    }

    // A negative literal, an expression, and a nested parenthesis.
    int negative(-5);
    if (negative != -5) {
        return 2;
    }
    // A nested parenthesis is an expression, so it is an initializer.
    int nested((whole));
    if (nested != 7) {
        return 4;
    }

    // A class constructed from a literal and from a name.
    Item item(7);
    if (item.read() != 7) {
        return 5;
    }
    Item from_name(whole);
    if (from_name.read() != 7) {
        return 6;
    }
    Item empty(0);
    if (empty.read() != 0) {
        return 7;
    }

    // A class template constructed from a `new` expression, which is the form
    // an owner type is written in.
    Owner<Item> owner(new Item(7));
    if (!owner.get()) {
        return 8;
    }
    if (owner.get()->read() != 7) {
        return 9;
    }

    // A real function declaration in the same translation unit is unaffected.
    return add(20, 22) == 42 ? 42 : 10;
}

int add(int left, int right) { return left + right; }
