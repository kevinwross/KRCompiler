struct S {
    static int values[3];
};

int S::values[3] = {1, 2, 3};

int main() {
    return S::values[1];
}
