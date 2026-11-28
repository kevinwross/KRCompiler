// Header-defined code for the kr216 source-file identity test: a member
// function written in a header must have its line records attributed to the
// header, not to the including .cpp.
struct Span {
    int first;
    int second;
    int sum() { return first + second; }
};
