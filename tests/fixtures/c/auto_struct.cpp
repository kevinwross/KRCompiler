struct S {
    int value;
};

int main() {
    S source;
    source.value = 3;
    auto copy = source;
    return copy.value;
}
