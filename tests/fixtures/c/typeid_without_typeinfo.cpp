// typeid needs the std::type_info declaration; without <typeinfo> the result
// has no type, so the compiler must name the missing header.
struct Plain {
    int value;
};

int main() {
    Plain plain;
    return typeid(plain).name()[0];
}
