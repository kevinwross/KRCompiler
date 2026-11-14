// Non-type template parameters (ADR 0033): `Buffer<int, 4>` and
// `Buffer<double, 2>` are two records whose array member has the bound the
// argument gives, and the parameter is usable as a value and as a size.
// Returns 42 when every bound and value holds.

template <typename T, int N>
class Buffer {
public:
    Buffer() : count(0) {}
    int capacity() const { return N; }
    bool push(T value) {
        if (count >= N) {
            return false;
        }
        data[count] = value;
        count = count + 1;
        return true;
    }
    T get(int index) const { return data[index]; }
    int size() const { return count; }

private:
    T data[N];
    int count;
};

int main() {
    Buffer<int, 4> whole;
    if (whole.capacity() != 4) {
        return 1;
    }
    if (!whole.push(7) || !whole.push(8)) {
        return 2;
    }
    if (whole.size() != 2) {
        return 3;
    }
    if (whole.get(0) != 7 || whole.get(1) != 8) {
        return 4;
    }
    // The bound is enforced.
    if (whole.push(9) && whole.push(10) && whole.push(11)) {
        return 5;
    }
    if (whole.capacity() != 4) {
        return 6;
    }

    // A second bound and element type is a different record.
    Buffer<double, 2> tiny;
    if (tiny.capacity() != 2) {
        return 7;
    }
    if (!tiny.push(1.5)) {
        return 8;
    }
    if (tiny.get(0) != 1.5) {
        return 9;
    }
    if (tiny.push(2.5) && tiny.push(3.5)) {
        return 10;
    }
    return 42;
}
