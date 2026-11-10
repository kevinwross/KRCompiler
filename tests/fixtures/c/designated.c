struct Pair { int x; int y; };

int main() {
    int values[4] = {[2] = 7, [0] = 1};
    struct Pair pair = {.y = 5, .x = 3};
    return values[0] + values[2] + pair.x + pair.y;
}
