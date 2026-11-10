class Calculator {
public:
    int add(int left, int right = 4) { return left + right; }
};

int main() {
    Calculator calculator;
    return calculator.add(3);
}
