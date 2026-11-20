/* Debug Adapter Protocol harness fixture.
 *
 * `total` is produced by a call rather than a literal so no level folds it away,
 * and `main` reaches its return statement with a known value in a named frame
 * slot -- the two things a debug session must report: a breakpoint that stops in
 * `main`, and a local that reads back the value the program computed.
 *
 * krc_debug_dap_session breaks on this file's `return total;` line by number, so a
 * change to its line numbering has to update that test. */

int compute(int value) {
    return value + 1;
}

int main(void) {
    int total = compute(41);
    return total;
}
