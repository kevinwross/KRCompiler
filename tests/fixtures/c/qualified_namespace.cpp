namespace math {
int base = 3;
int add(int left, int right) { return left + right; }
}

int main() {
    return math::add(math::base, 4);
}
