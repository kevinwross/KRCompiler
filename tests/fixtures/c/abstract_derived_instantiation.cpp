struct Shape {
    virtual int area() const = 0;
    virtual ~Shape() {}
};
struct Square : Shape {
    int side;
    Square() : side(3) {}
    int area() const override { return side; }
};
int main() {
    Square square;
    return square.area();
}
