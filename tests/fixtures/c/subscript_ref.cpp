class Values {
public:
    Values() { values_[0] = 1; values_[1] = 2; }
    int& operator[](int index) { return values_[index]; }
private:
    int values_[2];
};
int main() {
    Values values;
    values[0] = 3;
    return values[0] + values[1];
}
