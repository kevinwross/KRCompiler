int destructions = 0;

struct Item {
    ~Item() {
        destructions++;
    }
};

int main() {
    Item *pointer = 0;
    delete pointer;
    Item *array = 0;
    delete[] array;
    return destructions;
}
