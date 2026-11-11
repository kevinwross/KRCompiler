struct Point {
    int x;
    int y;
};

int main(void) {
    struct Point point = (struct Point){ 3, 4 };
    return point.x + point.y;
}
