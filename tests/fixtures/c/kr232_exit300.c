/* A test program that exits with a code above a byte: the harness reads the
   process status back, so this fails if the status is truncated anywhere. */
int main(void) {
    return 300;
}

