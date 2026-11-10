enum class Color : int { Red, Green };
alignas(16) int value = 3;

int main() {
    Color color = Color::Green;
    return value + (int)color;
}
