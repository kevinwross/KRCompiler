namespace math {
int add(int left, int right) { return left + right; }
}

using Number = int;

int main() {
    Number value = 3;
    return add(value, 4);
}
