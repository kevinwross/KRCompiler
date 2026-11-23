struct pair {
    int a;
    int b;
};

int frame_objects(int n) {
    unsigned block[3];
    double value;
    struct pair p;
    block[0] = (unsigned)n;
    value = 1.5;
    p.a = (int)block[0];
    p.b = (int)value;
    return p.a + p.b;
}

int main(void) {
    return frame_objects(1) == 2 ? 0 : 1;
}
