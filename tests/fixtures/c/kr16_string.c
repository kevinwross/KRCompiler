int main(void) {
    char *text = "hi";
    return text[0] == 'h' && text[1] == 'i' && text[2] == 0 ? 0 : 1;
}
