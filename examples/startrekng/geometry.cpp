#include "geometry.hpp"

int stDistance(int x1, int y1, int x2, int y2) {
    int ax = x1 > x2 ? x1 - x2 : x2 - x1;
    int ay = y1 > y2 ? y1 - y2 : y2 - y1;
    return ax > ay ? ax : ay;
}

int stCourse(int x1, int y1, int x2, int y2) {
    int ddx = x2 - x1;
    int ddy = y2 - y1;
    int ax = ddx < 0 ? -ddx : ddx;
    int ay = ddy < 0 ? -ddy : ddy;
    if (ddx == 0 && ddy == 0) {
        return 0;
    }
    if (ddx == 0) {
        return ddy < 0 ? 1 : 5;
    }
    if (ddy == 0) {
        return ddx > 0 ? 3 : 7;
    }
    if (ax > ay * 2) {
        return ddx > 0 ? 3 : 7;
    }
    if (ay > ax * 2) {
        return ddy < 0 ? 1 : 5;
    }
    if (ddx > 0) {
        return ddy < 0 ? 2 : 4;
    }
    return ddy < 0 ? 8 : 6;
}
