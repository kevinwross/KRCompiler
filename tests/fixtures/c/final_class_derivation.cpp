struct Locked final {
    int value;
};
struct Attempt : Locked {
    int other;
};
int main() { Attempt object; return 0; }
