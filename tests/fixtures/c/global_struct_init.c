struct Point {
    int x;
    int y;
};

struct Point point = { 3, 4 };

int main(void) {
    return point.x + point.y;
}
