class Point {
public:
    int x;
    int y;
};

Point make_point() {
    Point point;
    point.x = 3;
    point.y = 4;
    return point;
}

int main() {
    Point point = make_point();
    return point.x + point.y;
}
