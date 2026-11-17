// Tier 2 — single inheritance. A base with state, a derived class that adds to it,
// construction through the base, and a base-class pointer that names a derived
// object.
//
// KR32-only, for the same reason as classes.cpp: this tier's OOP fixtures need the
// C++ ABI that KR16 defers.

class Shape {
public:
    explicit Shape(int sides) : sides_(sides) { }

    int sides() const {
        return sides_;
    }

    int doubled() const {
        return sides_ * 2;
    }

protected:
    int sides_;
};

class Quad : public Shape {
public:
    Quad() : Shape(4), width_(0) { }

    explicit Quad(int width) : Shape(4), width_(width) { }

    int width() const {
        return width_;
    }

    // The base's members are reachable from the derived class.
    int perimeter() const {
        return sides_ * width_;
    }

private:
    int width_;
};

int main() {
    Quad square(5);
    if (square.sides() != 4) { return 1; }
    if (square.width() != 5) { return 2; }
    if (square.perimeter() != 20) { return 3; }

    // An inherited member is callable on the derived object without qualification.
    if (square.doubled() != 8) { return 4; }

    // A base-class pointer to a derived object reaches the base's members.
    Shape* shape = &square;
    if (shape->sides() != 4) { return 5; }
    if (shape->doubled() != 8) { return 6; }

    // A derived object is constructed through its base: the base's constructor ran.
    Quad defaulted;
    if (defaulted.sides() != 4) { return 7; }
    if (defaulted.width() != 0) { return 8; }

    return 0;
}
