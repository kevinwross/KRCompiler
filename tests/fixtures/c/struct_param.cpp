class Point {
public:
    int x;
    int y;
};
int sum(Point point) { return point.x + point.y; }
int main() {
    Point point;
    point.x = 3;
    point.y = 4;
    return sum(point);
}
