struct S {
    int data[3];
    int &operator[](int index);
};

int &S::operator[](int index) { return data[index]; }

int main() {
    S s;
    s.data[1] = 5;
    return s[1];
}
