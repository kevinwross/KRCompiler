struct Value {
    int value;

    Value(int input) : value(input) {}
};

struct Holder {
    Value member{7};
};

int main() {
    Holder holder;
    return holder.member.value;
}
