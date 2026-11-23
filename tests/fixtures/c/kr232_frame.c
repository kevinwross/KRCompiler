/* One local, so the frame is exactly one 4-byte slot. This is the frame-size
   check: the prologue's ENTER immediate is the frame the ABI lays out. */
int main(void) {
    int value = 1;
    return value;
}
