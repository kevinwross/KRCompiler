class Point {
public:
    Point(int x, int y) : x_(x), y_(y) {}
    int sum() const { return x_ + y_; }

private:
    int x_;
    int y_;
};

int main() {
    Point point(3, 4);
    return point.sum();
}
