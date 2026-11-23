/* The frame check returns to a one-slot frame when locals compile; until load and store exist, the backend accepts only a constant return, so this program's frame is zero. */
int main(void) { return 42; }

