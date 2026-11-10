namespace outer {
namespace inner {
int value = 5;
}
}
int main() {
    return outer::inner::value;
}
