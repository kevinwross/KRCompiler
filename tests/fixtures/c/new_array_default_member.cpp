class Value {
public:
    int number = 5;
};

int main() {
    Value *values = new Value[2];
    int total = values[0].number + values[1].number;
    delete[] values;
    return total;
}
