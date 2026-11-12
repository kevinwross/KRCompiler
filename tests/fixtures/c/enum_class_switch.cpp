enum class Color { Red = 1, Green = 2 };
int main() { Color color = Color::Green; switch (color) { case Color::Green: return 7; default: return 1; } }
