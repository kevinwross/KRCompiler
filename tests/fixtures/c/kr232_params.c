/* By-value parameters: a char and a short are passed in R2 and R3 and returned
   through R2, so the exit status proves both the call and the narrow load. */
int widen(char c, short s) {
    return c + s;
}

int main(void) {
    return widen(65, 0);
}
