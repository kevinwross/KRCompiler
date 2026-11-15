/* Extracted from ../kr32/examples at commit ba69940 for the Tier 0-1 corpus.
 * Copied verbatim; the original header, if any, follows. */
class Point {
public:
    int x;
    int y;

    int sum() {
        return x + y;
    }
};

int main() {
    Point point;
    point.x = 20;
    point.y = 22;
    return point.sum();
}
