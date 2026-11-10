class Values {
public:
    Values() {
        values_[0] = 3;
        values_[1] = 4;
    }
    int operator[](int index) { return values_[index]; }

private:
    int values_[2];
};

int main() {
    Values values;
    return values[0] + values[1];
}
