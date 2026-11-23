/* A byte stored to a frame slot and loaded back: the load and store paths meet
   here, and the returned value proves the addressing rather than the constant. */
int main(void) {
    char c = 65;
    return c;
}
