class Value {
public:
    int number = 5;
};

int main() {
    Value *value = new Value();
    int number = value->number;
    delete value;
    return number;
}
