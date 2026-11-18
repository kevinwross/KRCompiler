// KRlibcpp.cpp - C++ code-generation benchmark, companion to src/cc/libc.c.
//
// Purpose: a broad, self-contained body of C++ that the kr32 C++ front end can
// compile today, so the emitted assembly can be compared across compiler drops
// the same way libc.c is. Nothing here is meant to run; there is no I/O, no
// runtime dependency, and no include.
//
// Rules this file follows, because the front end does not support everything:
//   * no standard headers, no std::, no external calls;
//   * aggregate initialisation uses constructors, not V{a, b} temporaries;
//   * no bit-fields, no pointer-to-member, no out-of-class static members;
//   * a reference is never produced by a ternary.
// Every externally visible name starts with KR so linking this against the
// runtime cannot collide.
//
// Compile:  kr32 cxx examples/libcpp.cpp -S -O1 -o libcpp.s
// Sections: 1 integer math, 2 bit operations, 3 strings, 4 memory,
//           5 arrays and sorting, 6 containers, 7 inheritance, 8 templates,
//           9 floating point, 10 errors, 11 hashing and statistics.

// ---------------------------------------------------------------- 1 integer math

int KRAbs(int value) {
    if (value < 0) {
        return -value;
    }
    return value;
}

long long KRAbsLL(long long value) {
    if (value < 0) {
        return -value;
    }
    return value;
}

int KRMin(int a, int b) {
    if (a < b) {
        return a;
    }
    return b;
}

int KRMax(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

long long KRMinLL(long long a, long long b) {
    if (a < b) {
        return a;
    }
    return b;
}

long long KRMaxLL(long long a, long long b) {
    if (a > b) {
        return a;
    }
    return b;
}

unsigned KRMinUnsigned(unsigned a, unsigned b) {
    if (a < b) {
        return a;
    }
    return b;
}

unsigned KRMaxUnsigned(unsigned a, unsigned b) {
    if (a > b) {
        return a;
    }
    return b;
}

int KRClamp(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

double KRClampDouble(double value, double low, double high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

int KRSign(int value) {
    if (value < 0) {
        return -1;
    }
    if (value > 0) {
        return 1;
    }
    return 0;
}

int KRIsEven(int value) {
    return (value & 1) == 0;
}

int KRIsOdd(int value) {
    return (value & 1) != 0;
}

int KRAbsDiff(int a, int b) {
    if (a > b) {
        return a - b;
    }
    return b - a;
}

int KRSquare(int value) {
    return value * value;
}

long long KRSquareLL(long long value) {
    return value * value;
}

int KRCube(int value) {
    return value * value * value;
}

int KRPowInt(int base, int exponent) {
    int result = 1;
    int factor = base;
    int remaining = exponent;
    while (remaining > 0) {
        if ((remaining & 1) != 0) {
            result = result * factor;
        }
        factor = factor * factor;
        remaining = remaining >> 1;
    }
    return result;
}

long long KRPowLL(long long base, int exponent) {
    long long result = 1;
    long long factor = base;
    int remaining = exponent;
    while (remaining > 0) {
        if ((remaining & 1) != 0) {
            result = result * factor;
        }
        factor = factor * factor;
        remaining = remaining >> 1;
    }
    return result;
}

int KRPowMod(int base, int exponent, int modulus) {
    int result = 1 % modulus;
    int factor = base % modulus;
    int remaining = exponent;
    while (remaining > 0) {
        if ((remaining & 1) != 0) {
            result = (result * factor) % modulus;
        }
        factor = (factor * factor) % modulus;
        remaining = remaining >> 1;
    }
    return result;
}

int KRDivMod(int dividend, int divisor, int* remainder) {
    int quotient = dividend / divisor;
    int rest = dividend - quotient * divisor;
    *remainder = rest;
    return quotient;
}

int KRFloorDiv(int dividend, int divisor) {
    int quotient = dividend / divisor;
    int rest = dividend - quotient * divisor;
    if (rest != 0 && ((rest < 0) != (divisor < 0))) {
        quotient = quotient - 1;
    }
    return quotient;
}

int KRCeilDiv(int dividend, int divisor) {
    int quotient = dividend / divisor;
    int rest = dividend - quotient * divisor;
    if (rest != 0 && ((rest < 0) == (divisor < 0))) {
        quotient = quotient + 1;
    }
    return quotient;
}

int KRModPositive(int dividend, int divisor) {
    int rest = dividend % divisor;
    if (rest < 0) {
        rest = rest + divisor;
    }
    return rest;
}

int KRGcd(int a, int b) {
    int left = KRAbs(a);
    int right = KRAbs(b);
    while (right != 0) {
        int next = left % right;
        left = right;
        right = next;
    }
    return left;
}

int KRLcm(int a, int b) {
    int divisor = KRGcd(a, b);
    if (divisor == 0) {
        return 0;
    }
    return KRAbs(a / divisor * b);
}

int KRIsPrime(int value) {
    if (value < 2) {
        return 0;
    }
    if (value < 4) {
        return 1;
    }
    if ((value & 1) == 0) {
        return 0;
    }
    int divisor = 3;
    while (divisor * divisor <= value) {
        if (value % divisor == 0) {
            return 0;
        }
        divisor = divisor + 2;
    }
    return 1;
}

int KRNextPrime(int value) {
    int candidate = value + 1;
    while (KRIsPrime(candidate) == 0) {
        candidate = candidate + 1;
    }
    return candidate;
}

int KRCountPrimes(int limit) {
    int count = 0;
    int candidate = 2;
    while (candidate < limit) {
        if (KRIsPrime(candidate) != 0) {
            count = count + 1;
        }
        candidate = candidate + 1;
    }
    return count;
}

int KRSieveSum(int limit) {
    int flags[256];
    int index = 0;
    while (index < 256) {
        flags[index] = 1;
        index = index + 1;
    }
    flags[0] = 0;
    flags[1] = 0;
    int factor = 2;
    while (factor * factor < limit) {
        if (flags[factor] != 0) {
            int multiple = factor * factor;
            while (multiple < limit) {
                flags[multiple] = 0;
                multiple = multiple + factor;
            }
        }
        factor = factor + 1;
    }
    int total = 0;
    index = 0;
    while (index < limit) {
        if (flags[index] != 0) {
            total = total + index;
        }
        index = index + 1;
    }
    return total;
}

int KRFib(int index) {
    if (index < 2) {
        return index;
    }
    return KRFib(index - 1) + KRFib(index - 2);
}

long long KRFibIter(int index) {
    long long previous = 0;
    long long current = 1;
    int step = 0;
    while (step < index) {
        long long next = previous + current;
        previous = current;
        current = next;
        step = step + 1;
    }
    return previous;
}

int KRFibMod(int index, int modulus) {
    int previous = 0;
    int current = 1;
    int step = 0;
    while (step < index) {
        int next = previous + current;
        if (next >= modulus) {
            next = next - modulus;
        }
        previous = current;
        current = next;
        step = step + 1;
    }
    return previous;
}

int KRCollatzSteps(int value) {
    int steps = 0;
    long long current = value;
    while (current != 1 && steps < 1000) {
        if ((current & 1) == 0) {
            current = current / 2;
        } else {
            current = current * 3 + 1;
        }
        steps = steps + 1;
    }
    return steps;
}

int KRDigitalSum(int value) {
    int total = 0;
    int rest = KRAbs(value);
    while (rest > 0) {
        total = total + rest % 10;
        rest = rest / 10;
    }
    return total;
}

int KRReverseDigits(int value) {
    int result = 0;
    int rest = KRAbs(value);
    while (rest > 0) {
        result = result * 10 + rest % 10;
        rest = rest / 10;
    }
    if (value < 0) {
        return -result;
    }
    return result;
}

int KRIsPalindromeNumber(int value) {
    return KRReverseDigits(value) == value;
}

int KRDigitCount(int value) {
    int count = 1;
    int rest = KRAbs(value);
    while (rest >= 10) {
        rest = rest / 10;
        count = count + 1;
    }
    return count;
}

int KRTriangular(int index) {
    return index * (index + 1) / 2;
}

int KRIsTriangular(int value) {
    int step = 1;
    while (step * (step + 1) / 2 < value) {
        step = step + 1;
    }
    return step * (step + 1) / 2 == value;
}

int KRBinomial(int n, int k) {
    if (k < 0 || k > n) {
        return 0;
    }
    int result = 1;
    int step = 1;
    while (step <= k) {
        result = result * (n - k + step) / step;
        step = step + 1;
    }
    return result;
}

int KRAckermannSmall(int m, int n) {
    if (m == 0) {
        return n + 1;
    }
    if (n == 0) {
        return KRAckermannSmall(m - 1, 1);
    }
    return KRAckermannSmall(m - 1, KRAckermannSmall(m, n - 1));
}

int KRIsPowerOfTwo(int value) {
    if (value <= 0) {
        return 0;
    }
    return (value & (value - 1)) == 0;
}

int KRNextPowerOfTwo(int value) {
    int result = 1;
    while (result < value) {
        result = result << 1;
    }
    return result;
}

int KRPrevPowerOfTwo(int value) {
    int result = 1;
    while ((result << 1) <= value) {
        result = result << 1;
    }
    return result;
}

int KRLog2Int(int value) {
    int result = 0;
    int remaining = value;
    while (remaining > 1) {
        remaining = remaining >> 1;
        result = result + 1;
    }
    return result;
}

int KRIntSqrt(int value) {
    if (value < 0) {
        return -1;
    }
    if (value < 2) {
        return value;
    }
    int low = 1;
    int high = value;
    while (low <= high) {
        int middle = low + (high - low) / 2;
        if (middle <= value / middle) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return high;
}

int KRLongLongSqrt(long long value) {
    if (value < 0) {
        return -1;
    }
    if (value < 2) {
        return (int)value;
    }
    long long low = 1;
    long long high = value;
    while (low <= high) {
        long long middle = low + (high - low) / 2;
        if (middle <= value / middle) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return (int)high;
}

int KRPerfectSquare(int value) {
    int root = KRIntSqrt(value);
    return root * root == value;
}

int KRSumSquares(int limit) {
    int total = 0;
    int index = 1;
    while (index <= limit) {
        total = total + index * index;
        index = index + 1;
    }
    return total;
}

int KREulerTotient(int value) {
    int result = value;
    int remaining = value;
    int factor = 2;
    while (factor * factor <= remaining) {
        if (remaining % factor == 0) {
            while (remaining % factor == 0) {
                remaining = remaining / factor;
            }
            result = result - result / factor;
        }
        factor = factor + 1;
    }
    if (remaining > 1) {
        result = result - result / remaining;
    }
    return result;
}

// ---------------------------------------------------------------- 2 bit operations

int KRPopCount(unsigned value) {
    unsigned remaining = value;
    int count = 0;
    while (remaining != 0) {
        remaining = remaining & (remaining - 1);
        count = count + 1;
    }
    return count;
}

int KRPopCountLL(unsigned long long value) {
    unsigned long long remaining = value;
    int count = 0;
    while (remaining != 0) {
        remaining = remaining & (remaining - 1);
        count = count + 1;
    }
    return count;
}

int KRLeadingZeros(unsigned value) {
    if (value == 0) {
        return 32;
    }
    int count = 0;
    unsigned remaining = value;
    while ((remaining & 0x80000000u) == 0) {
        remaining = remaining << 1;
        count = count + 1;
    }
    return count;
}

int KRTrailingZeros(unsigned value) {
    if (value == 0) {
        return 32;
    }
    int count = 0;
    unsigned remaining = value;
    while ((remaining & 1u) == 0) {
        remaining = remaining >> 1;
        count = count + 1;
    }
    return count;
}

unsigned KRRotateLeft(unsigned value, int amount) {
    int shift = amount & 31;
    if (shift == 0) {
        return value;
    }
    return (value << shift) | (value >> (32 - shift));
}

unsigned KRRotateRight(unsigned value, int amount) {
    int shift = amount & 31;
    if (shift == 0) {
        return value;
    }
    return (value >> shift) | (value << (32 - shift));
}

unsigned KRReverseBits(unsigned value) {
    unsigned result = 0;
    unsigned remaining = value;
    int index = 0;
    while (index < 32) {
        result = (result << 1) | (remaining & 1u);
        remaining = remaining >> 1;
        index = index + 1;
    }
    return result;
}

unsigned KRByteSwap(unsigned value) {
    unsigned byte0 = (value >> 24) & 0xFFu;
    unsigned byte1 = (value >> 16) & 0xFFu;
    unsigned byte2 = (value >> 8) & 0xFFu;
    unsigned byte3 = value & 0xFFu;
    return (byte3 << 24) | (byte2 << 16) | (byte1 << 8) | byte0;
}

unsigned KRSwapHalves(unsigned value) {
    return (value << 16) | (value >> 16);
}

unsigned KRLowestBit(unsigned value) {
    return value & (0u - value);
}

int KRParity(unsigned value) {
    unsigned remaining = value;
    int parity = 0;
    while (remaining != 0) {
        parity = parity ^ (int)(remaining & 1u);
        remaining = remaining >> 1;
    }
    return parity;
}

unsigned KRMaskRange(int low, int high) {
    if (low < 0 || high > 31 || low > high) {
        return 0;
    }
    unsigned width = (unsigned)(high - low + 1);
    unsigned mask = 1u;
    int index = 1;
    while (index < (int)width) {
        mask = (mask << 1) | 1u;
        index = index + 1;
    }
    return mask << low;
}

unsigned KRExtractBits(unsigned value, int low, int count) {
    if (count <= 0 || low < 0 || low + count > 32) {
        return 0;
    }
    unsigned mask = KRMaskRange(low, low + count - 1);
    return (value & mask) >> low;
}

unsigned KRInsertBits(unsigned value, unsigned field, int low, int count) {
    if (count <= 0 || low < 0 || low + count > 32) {
        return value;
    }
    unsigned mask = KRMaskRange(low, low + count - 1);
    unsigned shifted = (field << low) & mask;
    return (value & ~mask) | shifted;
}

int KRBitWidth(unsigned value) {
    if (value == 0) {
        return 0;
    }
    return 32 - KRLeadingZeros(value);
}

int KRBitCountBetween(unsigned low, unsigned high) {
    int total = 0;
    unsigned index = low;
    while (index <= high) {
        total = total + KRPopCount(index);
        index = index + 1;
    }
    return total;
}

unsigned KRGrayCode(unsigned value) {
    return value ^ (value >> 1);
}

unsigned KRFromGray(unsigned value) {
    unsigned result = value;
    unsigned shift = value >> 1;
    while (shift != 0) {
        result = result ^ shift;
        shift = shift >> 1;
    }
    return result;
}

unsigned KRSmearBits(unsigned value) {
    unsigned result = value;
    result = result | (result >> 1);
    result = result | (result >> 2);
    result = result | (result >> 4);
    result = result | (result >> 8);
    result = result | (result >> 16);
    return result;
}

unsigned KRNextPermutationMask(unsigned value) {
    if (value == 0) {
        return 1;
    }
    unsigned lowest = KRLowestBit(value);
    unsigned ripple = value + lowest;
    unsigned ones = value ^ ripple;
    ones = (ones >> 2) / lowest;
    return ripple | ones;
}

int KRCountBitChanges(unsigned value) {
    unsigned previous = value & 1u;
    unsigned remaining = value >> 1;
    int changes = 0;
    while (remaining != 0) {
        unsigned current = remaining & 1u;
        if (current != previous) {
            changes = changes + 1;
        }
        previous = current;
        remaining = remaining >> 1;
    }
    return changes;
}

unsigned KRRotateBytes(unsigned value, int amount) {
    int shift = ((amount % 4) + 4) % 4;
    int index = 0;
    unsigned result = value;
    while (index < shift) {
        result = (result << 8) | (result >> 24);
        index = index + 1;
    }
    return result;
}

// ---------------------------------------------------------------- 3 strings

int KRStrLen(const char* text) {
    int length = 0;
    while (text[length] != 0) {
        length = length + 1;
    }
    return length;
}

int KRStrCpy(char* destination, const char* source) {
    int index = 0;
    while (source[index] != 0) {
        destination[index] = source[index];
        index = index + 1;
    }
    destination[index] = 0;
    return index;
}

int KRNStrCpy(char* destination, const char* source, int count) {
    int index = 0;
    while (index < count - 1 && source[index] != 0) {
        destination[index] = source[index];
        index = index + 1;
    }
    destination[index] = 0;
    return index;
}

int KRStrCat(char* destination, const char* source) {
    int length = KRStrLen(destination);
    int index = 0;
    while (source[index] != 0) {
        destination[length + index] = source[index];
        index = index + 1;
    }
    destination[length + index] = 0;
    return length + index;
}

int KRStrCmp(const char* left, const char* right) {
    int index = 0;
    while (left[index] != 0 && right[index] != 0) {
        if (left[index] != right[index]) {
            return (int)left[index] - (int)right[index];
        }
        index = index + 1;
    }
    return (int)left[index] - (int)right[index];
}

int KRStrNCmp(const char* left, const char* right, int count) {
    int index = 0;
    while (index < count && left[index] != 0 && right[index] != 0) {
        if (left[index] != right[index]) {
            return (int)left[index] - (int)right[index];
        }
        index = index + 1;
    }
    if (index == count) {
        return 0;
    }
    return (int)left[index] - (int)right[index];
}

const char* KRStrChr(const char* text, char wanted) {
    int index = 0;
    while (text[index] != 0) {
        if (text[index] == wanted) {
            return text + index;
        }
        index = index + 1;
    }
    return nullptr;
}

const char* KRStrRChr(const char* text, char wanted) {
    int length = KRStrLen(text);
    int index = length - 1;
    while (index >= 0) {
        if (text[index] == wanted) {
            return text + index;
        }
        index = index - 1;
    }
    return nullptr;
}

const char* KRStrStr(const char* text, const char* wanted) {
    int length = KRStrLen(wanted);
    if (length == 0) {
        return text;
    }
    int index = 0;
    while (text[index] != 0) {
        int match = 0;
        while (match < length && text[index + match] == wanted[match]) {
            match = match + 1;
        }
        if (match == length) {
            return text + index;
        }
        index = index + 1;
    }
    return nullptr;
}

int KRStrCount(const char* text, const char* wanted) {
    int length = KRStrLen(wanted);
    if (length == 0) {
        return 0;
    }
    int count = 0;
    int index = 0;
    while (text[index] != 0) {
        int match = 0;
        while (match < length && text[index + match] == wanted[match]) {
            match = match + 1;
        }
        if (match == length) {
            count = count + 1;
            index = index + length;
        } else {
            index = index + 1;
        }
    }
    return count;
}

int KRStrRev(char* text) {
    int length = KRStrLen(text);
    int left = 0;
    int right = length - 1;
    while (left < right) {
        char temporary = text[left];
        text[left] = text[right];
        text[right] = temporary;
        left = left + 1;
        right = right - 1;
    }
    return length;
}

char KRToUpper(char value) {
    if (value >= 'a' && value <= 'z') {
        return (char)(value - 'a' + 'A');
    }
    return value;
}

char KRToLower(char value) {
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

int KRStrUpper(char* text) {
    int index = 0;
    while (text[index] != 0) {
        text[index] = KRToUpper(text[index]);
        index = index + 1;
    }
    return index;
}

int KRStrLower(char* text) {
    int index = 0;
    while (text[index] != 0) {
        text[index] = KRToLower(text[index]);
        index = index + 1;
    }
    return index;
}

int KRIsDigit(char value) {
    return value >= '0' && value <= '9';
}

int KRIsAlpha(char value) {
    if (value >= 'a' && value <= 'z') {
        return 1;
    }
    if (value >= 'A' && value <= 'Z') {
        return 1;
    }
    return 0;
}

int KRIsSpace(char value) {
    if (value == ' ' || value == '\t' || value == '\n' || value == '\r') {
        return 1;
    }
    return 0;
}

int KRIsAlnum(char value) {
    if (KRIsAlpha(value) != 0) {
        return 1;
    }
    return KRIsDigit(value);
}

const char* KRTrimLeft(const char* text) {
    int index = 0;
    while (text[index] != 0 && KRIsSpace(text[index]) != 0) {
        index = index + 1;
    }
    return text + index;
}

int KRTrimRight(char* text) {
    int length = KRStrLen(text);
    while (length > 0 && KRIsSpace(text[length - 1]) != 0) {
        length = length - 1;
    }
    text[length] = 0;
    return length;
}

int KRTrim(char* text) {
    const char* start = KRTrimLeft(text);
    int length = KRStrLen(start);
    int index = 0;
    while (index < length) {
        text[index] = start[index];
        index = index + 1;
    }
    text[length] = 0;
    return KRTrimRight(text);
}

int KRStrToInt(const char* text, int* consumed) {
    int index = 0;
    int sign = 1;
    if (text[index] == '-') {
        sign = -1;
        index = index + 1;
    } else if (text[index] == '+') {
        index = index + 1;
    }
    int value = 0;
    while (KRIsDigit(text[index]) != 0) {
        value = value * 10 + (int)(text[index] - '0');
        index = index + 1;
    }
    if (consumed != nullptr) {
        *consumed = index;
    }
    return value * sign;
}

unsigned KRStrToUnsigned(const char* text, int* consumed) {
    int index = 0;
    unsigned value = 0;
    while (KRIsDigit(text[index]) != 0) {
        value = value * 10u + (unsigned)(text[index] - '0');
        index = index + 1;
    }
    if (consumed != nullptr) {
        *consumed = index;
    }
    return value;
}

int KRIntToStr(int value, char* buffer, int capacity) {
    char digits[12];
    int count = 0;
    int remaining = KRAbs(value);
    if (remaining == 0) {
        digits[count] = '0';
        count = count + 1;
    }
    while (remaining > 0) {
        digits[count] = (char)('0' + remaining % 10);
        remaining = remaining / 10;
        count = count + 1;
    }
    int needed = count;
    if (value < 0) {
        needed = needed + 1;
    }
    if (needed + 1 > capacity) {
        return 0;
    }
    int index = 0;
    if (value < 0) {
        buffer[index] = '-';
        index = index + 1;
    }
    int digit = count - 1;
    while (digit >= 0) {
        buffer[index] = digits[digit];
        index = index + 1;
        digit = digit - 1;
    }
    buffer[index] = 0;
    return index;
}

int KRUIntToStr(unsigned value, char* buffer, int capacity) {
    char digits[12];
    int count = 0;
    if (value == 0) {
        digits[count] = '0';
        count = count + 1;
    }
    while (value > 0) {
        digits[count] = (char)('0' + (int)(value % 10u));
        value = value / 10u;
        count = count + 1;
    }
    if (count + 1 > capacity) {
        return 0;
    }
    int index = 0;
    int digit = count - 1;
    while (digit >= 0) {
        buffer[index] = digits[digit];
        index = index + 1;
        digit = digit - 1;
    }
    buffer[index] = 0;
    return index;
}

int KRIntToHex(unsigned value, char* buffer, int capacity) {
    const char* digits = "0123456789ABCDEF";
    if (11 > capacity) {
        return 0;
    }
    buffer[0] = '0';
    buffer[1] = 'x';
    int index = 2;
    int shift = 28;
    int started = 0;
    while (shift >= 0) {
        unsigned nibble = (value >> shift) & 0xFu;
        if (nibble != 0 || started != 0 || shift == 0) {
            buffer[index] = digits[nibble];
            index = index + 1;
            started = 1;
        }
        shift = shift - 4;
    }
    buffer[index] = 0;
    return index;
}

int KRSplitCount(const char* text, char separator) {
    int count = 0;
    int index = 0;
    int in_word = 0;
    while (text[index] != 0) {
        if (text[index] == separator) {
            in_word = 0;
        } else if (in_word == 0) {
            in_word = 1;
            count = count + 1;
        }
        index = index + 1;
    }
    return count;
}

int KRCountChar(const char* text, char wanted) {
    int count = 0;
    int index = 0;
    while (text[index] != 0) {
        if (text[index] == wanted) {
            count = count + 1;
        }
        index = index + 1;
    }
    return count;
}

int KRWordCount(const char* text) {
    int count = 0;
    int index = 0;
    int in_word = 0;
    while (text[index] != 0) {
        if (KRIsSpace(text[index]) != 0) {
            in_word = 0;
        } else if (in_word == 0) {
            in_word = 1;
            count = count + 1;
        }
        index = index + 1;
    }
    return count;
}

int KRStrPrefix(const char* text, const char* prefix) {
    int index = 0;
    while (prefix[index] != 0) {
        if (text[index] != prefix[index]) {
            return 0;
        }
        index = index + 1;
    }
    return 1;
}

int KRStrSuffix(const char* text, const char* suffix) {
    int text_length = KRStrLen(text);
    int suffix_length = KRStrLen(suffix);
    if (suffix_length > text_length) {
        return 0;
    }
    int index = 0;
    while (index < suffix_length) {
        if (text[text_length - suffix_length + index] != suffix[index]) {
            return 0;
        }
        index = index + 1;
    }
    return 1;
}

int KRStrReplace(char* text, char from, char to) {
    int replaced = 0;
    int index = 0;
    while (text[index] != 0) {
        if (text[index] == from) {
            text[index] = to;
            replaced = replaced + 1;
        }
        index = index + 1;
    }
    return replaced;
}

int KRStrIsPalindrome(const char* text) {
    int length = KRStrLen(text);
    int left = 0;
    int right = length - 1;
    while (left < right) {
        char first = KRToLower(text[left]);
        char second = KRToLower(text[right]);
        if (first != second) {
            return 0;
        }
        left = left + 1;
        right = right - 1;
    }
    return 1;
}

int KRStrAnagram(const char* left, const char* right) {
    int counts[26];
    int index = 0;
    while (index < 26) {
        counts[index] = 0;
        index = index + 1;
    }
    index = 0;
    while (left[index] != 0) {
        char folded = KRToLower(left[index]);
        if (folded >= 'a' && folded <= 'z') {
            counts[folded - 'a'] = counts[folded - 'a'] + 1;
        }
        index = index + 1;
    }
    index = 0;
    while (right[index] != 0) {
        char folded = KRToLower(right[index]);
        if (folded >= 'a' && folded <= 'z') {
            counts[folded - 'a'] = counts[folded - 'a'] - 1;
        }
        index = index + 1;
    }
    index = 0;
    while (index < 26) {
        if (counts[index] != 0) {
            return 0;
        }
        index = index + 1;
    }
    return 1;
}

int KRStrLevenshtein(const char* left, const char* right) {
    int grid[33][33];
    int left_length = KRStrLen(left);
    int right_length = KRStrLen(right);
    int row = 0;
    while (row <= left_length) {
        grid[row][0] = row;
        row = row + 1;
    }
    int column = 0;
    while (column <= right_length) {
        grid[0][column] = column;
        column = column + 1;
    }
    row = 1;
    while (row <= left_length) {
        column = 1;
        while (column <= right_length) {
            int cost = 1;
            if (left[row - 1] == right[column - 1]) {
                cost = 0;
            }
            int deletion = grid[row - 1][column] + 1;
            int insertion = grid[row][column - 1] + 1;
            int substitution = grid[row - 1][column - 1] + cost;
            int best = deletion;
            if (insertion < best) {
                best = insertion;
            }
            if (substitution < best) {
                best = substitution;
            }
            grid[row][column] = best;
            column = column + 1;
        }
        row = row + 1;
    }
    return grid[left_length][right_length];
}

int KRStrHash(const char* text) {
    unsigned hash = 2166136261u;
    int index = 0;
    while (text[index] != 0) {
        hash = hash ^ (unsigned)(unsigned char)text[index];
        hash = hash * 16777619u;
        index = index + 1;
    }
    return (int)(hash & 0x7FFFFFFFu);
}

// ---------------------------------------------------------------- 4 memory

void KRMemCpy(void* destination, const void* source, int count) {
    unsigned char* to = (unsigned char*)destination;
    const unsigned char* from = (const unsigned char*)source;
    int index = 0;
    while (index < count) {
        to[index] = from[index];
        index = index + 1;
    }
}

void KRMemMove(void* destination, const void* source, int count) {
    unsigned char* to = (unsigned char*)destination;
    const unsigned char* from = (const unsigned char*)source;
    if (to == from || count <= 0) {
        return;
    }
    if (to < from) {
        int index = 0;
        while (index < count) {
            to[index] = from[index];
            index = index + 1;
        }
        return;
    }
    int index = count - 1;
    while (index >= 0) {
        to[index] = from[index];
        index = index - 1;
    }
}

void KRMemSet(void* destination, int value, int count) {
    unsigned char* to = (unsigned char*)destination;
    unsigned char byte = (unsigned char)value;
    int index = 0;
    while (index < count) {
        to[index] = byte;
        index = index + 1;
    }
}

void KRMemZero(void* destination, int count) {
    KRMemSet(destination, 0, count);
}

int KRMemCmp(const void* left, const void* right, int count) {
    const unsigned char* a = (const unsigned char*)left;
    const unsigned char* b = (const unsigned char*)right;
    int index = 0;
    while (index < count) {
        if (a[index] != b[index]) {
            return (int)a[index] - (int)b[index];
        }
        index = index + 1;
    }
    return 0;
}

const void* KRMemChr(const void* block, int value, int count) {
    const unsigned char* bytes = (const unsigned char*)block;
    unsigned char wanted = (unsigned char)value;
    int index = 0;
    while (index < count) {
        if (bytes[index] == wanted) {
            return bytes + index;
        }
        index = index + 1;
    }
    return nullptr;
}

void KRMemReverse(void* block, int count) {
    unsigned char* bytes = (unsigned char*)block;
    int left = 0;
    int right = count - 1;
    while (left < right) {
        unsigned char temporary = bytes[left];
        bytes[left] = bytes[right];
        bytes[right] = temporary;
        left = left + 1;
        right = right - 1;
    }
}

void KRMemSwapBytes(void* left, void* right, int count) {
    unsigned char* a = (unsigned char*)left;
    unsigned char* b = (unsigned char*)right;
    int index = 0;
    while (index < count) {
        unsigned char temporary = a[index];
        a[index] = b[index];
        b[index] = temporary;
        index = index + 1;
    }
}

void KRMemFillPattern(void* destination, int count, int period) {
    if (period <= 0) {
        return;
    }
    unsigned char* bytes = (unsigned char*)destination;
    int index = 0;
    while (index < count) {
        bytes[index] = (unsigned char)(index % period);
        index = index + 1;
    }
}

int KRMemCountByte(const void* block, int count, int value) {
    const unsigned char* bytes = (const unsigned char*)block;
    unsigned char wanted = (unsigned char)value;
    int found = 0;
    int index = 0;
    while (index < count) {
        if (bytes[index] == wanted) {
            found = found + 1;
        }
        index = index + 1;
    }
    return found;
}

// ---------------------------------------------------------------- 5 arrays and sorting

int KRSumInts(const int* data, int count) {
    int total = 0;
    int index = 0;
    while (index < count) {
        total = total + data[index];
        index = index + 1;
    }
    return total;
}

long long KRSumLongs(const long long* data, int count) {
    long long total = 0;
    int index = 0;
    while (index < count) {
        total = total + data[index];
        index = index + 1;
    }
    return total;
}

int KRProductInts(const int* data, int count) {
    int total = 1;
    int index = 0;
    while (index < count) {
        total = total * data[index];
        index = index + 1;
    }
    return total;
}

int KRMinIndex(const int* data, int count) {
    if (count <= 0) {
        return -1;
    }
    int best = 0;
    int index = 1;
    while (index < count) {
        if (data[index] < data[best]) {
            best = index;
        }
        index = index + 1;
    }
    return best;
}

int KRMaxIndex(const int* data, int count) {
    if (count <= 0) {
        return -1;
    }
    int best = 0;
    int index = 1;
    while (index < count) {
        if (data[index] > data[best]) {
            best = index;
        }
        index = index + 1;
    }
    return best;
}

int KRMinValue(const int* data, int count) {
    int index = KRMinIndex(data, count);
    if (index < 0) {
        return 0;
    }
    return data[index];
}

int KRMaxValue(const int* data, int count) {
    int index = KRMaxIndex(data, count);
    if (index < 0) {
        return 0;
    }
    return data[index];
}

int KRCountIfEqual(const int* data, int count, int wanted) {
    int found = 0;
    int index = 0;
    while (index < count) {
        if (data[index] == wanted) {
            found = found + 1;
        }
        index = index + 1;
    }
    return found;
}

int KRCountIfGreater(const int* data, int count, int limit) {
    int found = 0;
    int index = 0;
    while (index < count) {
        if (data[index] > limit) {
            found = found + 1;
        }
        index = index + 1;
    }
    return found;
}

int KRFindValue(const int* data, int count, int wanted) {
    int index = 0;
    while (index < count) {
        if (data[index] == wanted) {
            return index;
        }
        index = index + 1;
    }
    return -1;
}

int KRFindLastValue(const int* data, int count, int wanted) {
    int index = count - 1;
    while (index >= 0) {
        if (data[index] == wanted) {
            return index;
        }
        index = index - 1;
    }
    return -1;
}

void KRSwapInts(int* left, int* right) {
    int temporary = *left;
    *left = *right;
    *right = temporary;
}

void KRReverseInts(int* data, int count) {
    int left = 0;
    int right = count - 1;
    while (left < right) {
        KRSwapInts(data + left, data + right);
        left = left + 1;
        right = right - 1;
    }
}

void KRRotateInts(int* data, int count, int amount) {
    if (count <= 1) {
        return;
    }
    int shift = ((amount % count) + count) % count;
    if (shift == 0) {
        return;
    }
    KRReverseInts(data, count);
    KRReverseInts(data, count - shift);
    KRReverseInts(data + (count - shift), shift);
}

int KRIsSorted(const int* data, int count) {
    int index = 1;
    while (index < count) {
        if (data[index - 1] > data[index]) {
            return 0;
        }
        index = index + 1;
    }
    return 1;
}

void KRBubbleSort(int* data, int count) {
    int pass = 0;
    while (pass < count - 1) {
        int index = 0;
        int swapped = 0;
        while (index < count - 1 - pass) {
            if (data[index] > data[index + 1]) {
                KRSwapInts(data + index, data + index + 1);
                swapped = 1;
            }
            index = index + 1;
        }
        if (swapped == 0) {
            return;
        }
        pass = pass + 1;
    }
}

void KRInsertionSort(int* data, int count) {
    int index = 1;
    while (index < count) {
        int value = data[index];
        int place = index - 1;
        while (place >= 0 && data[place] > value) {
            data[place + 1] = data[place];
            place = place - 1;
        }
        data[place + 1] = value;
        index = index + 1;
    }
}

void KRSelectionSort(int* data, int count) {
    int index = 0;
    while (index < count - 1) {
        int best = index;
        int scan = index + 1;
        while (scan < count) {
            if (data[scan] < data[best]) {
                best = scan;
            }
            scan = scan + 1;
        }
        if (best != index) {
            KRSwapInts(data + index, data + best);
        }
        index = index + 1;
    }
}

void KRShellSort(int* data, int count) {
    int gap = count / 2;
    while (gap > 0) {
        int index = gap;
        while (index < count) {
            int value = data[index];
            int place = index;
            while (place >= gap && data[place - gap] > value) {
                data[place] = data[place - gap];
                place = place - gap;
            }
            data[place] = value;
            index = index + 1;
        }
        gap = gap / 2;
    }
}

int KRPartition(int* data, int low, int high) {
    int pivot = data[high];
    int boundary = low - 1;
    int index = low;
    while (index < high) {
        if (data[index] <= pivot) {
            boundary = boundary + 1;
            KRSwapInts(data + boundary, data + index);
        }
        index = index + 1;
    }
    KRSwapInts(data + boundary + 1, data + high);
    return boundary + 1;
}

void KRQuickSortRange(int* data, int low, int high) {
    if (low >= high) {
        return;
    }
    int pivot = KRPartition(data, low, high);
    KRQuickSortRange(data, low, pivot - 1);
    KRQuickSortRange(data, pivot + 1, high);
}

void KRQuickSort(int* data, int count) {
    if (count > 1) {
        KRQuickSortRange(data, 0, count - 1);
    }
}

void KRMergeRanges(int* data, int* scratch, int low, int middle, int high) {
    int left = low;
    int right = middle + 1;
    int out = low;
    while (left <= middle && right <= high) {
        if (data[left] <= data[right]) {
            scratch[out] = data[left];
            left = left + 1;
        } else {
            scratch[out] = data[right];
            right = right + 1;
        }
        out = out + 1;
    }
    while (left <= middle) {
        scratch[out] = data[left];
        left = left + 1;
        out = out + 1;
    }
    while (right <= high) {
        scratch[out] = data[right];
        right = right + 1;
        out = out + 1;
    }
    int index = low;
    while (index <= high) {
        data[index] = scratch[index];
        index = index + 1;
    }
}

void KRMergeSortRange(int* data, int* scratch, int low, int high) {
    if (low >= high) {
        return;
    }
    int middle = low + (high - low) / 2;
    KRMergeSortRange(data, scratch, low, middle);
    KRMergeSortRange(data, scratch, middle + 1, high);
    KRMergeRanges(data, scratch, low, middle, high);
}

void KRMergeSort(int* data, int* scratch, int count) {
    if (count > 1) {
        KRMergeSortRange(data, scratch, 0, count - 1);
    }
}

void KRSiftDown(int* data, int root, int count) {
    int current = root;
    while (1) {
        int left = current * 2 + 1;
        int right = left + 1;
        int largest = current;
        if (left < count && data[left] > data[largest]) {
            largest = left;
        }
        if (right < count && data[right] > data[largest]) {
            largest = right;
        }
        if (largest == current) {
            return;
        }
        KRSwapInts(data + current, data + largest);
        current = largest;
    }
}

void KRHeapSort(int* data, int count) {
    int start = count / 2 - 1;
    while (start >= 0) {
        KRSiftDown(data, start, count);
        start = start - 1;
    }
    int end = count - 1;
    while (end > 0) {
        KRSwapInts(data, data + end);
        KRSiftDown(data, 0, end);
        end = end - 1;
    }
}

int KRBinarySearch(const int* data, int count, int wanted) {
    int low = 0;
    int high = count - 1;
    while (low <= high) {
        int middle = low + (high - low) / 2;
        if (data[middle] == wanted) {
            return middle;
        }
        if (data[middle] < wanted) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return -1;
}

int KRLowerBound(const int* data, int count, int wanted) {
    int low = 0;
    int high = count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (data[middle] < wanted) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

int KRUpperBound(const int* data, int count, int wanted) {
    int low = 0;
    int high = count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (data[middle] <= wanted) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

int KRInsertSorted(int* data, int count, int capacity, int value) {
    if (count >= capacity) {
        return count;
    }
    int place = count;
    while (place > 0 && data[place - 1] > value) {
        data[place] = data[place - 1];
        place = place - 1;
    }
    data[place] = value;
    return count + 1;
}

int KRRemoveAt(int* data, int count, int index) {
    if (index < 0 || index >= count) {
        return count;
    }
    int place = index;
    while (place < count - 1) {
        data[place] = data[place + 1];
        place = place + 1;
    }
    return count - 1;
}

int KRUniqueSorted(int* data, int count) {
    if (count <= 1) {
        return count;
    }
    int write = 1;
    int read = 1;
    while (read < count) {
        if (data[read] != data[write - 1]) {
            data[write] = data[read];
            write = write + 1;
        }
        read = read + 1;
    }
    return write;
}

int KRCompactNonZero(int* data, int count) {
    int write = 0;
    int read = 0;
    while (read < count) {
        if (data[read] != 0) {
            data[write] = data[read];
            write = write + 1;
        }
        read = read + 1;
    }
    return write;
}

int KRRunLength(const int* data, int count) {
    int runs = 0;
    int index = 0;
    while (index < count) {
        int value = data[index];
        while (index < count && data[index] == value) {
            index = index + 1;
        }
        runs = runs + 1;
    }
    return runs;
}

int KRSumEven(const int* data, int count) {
    int total = 0;
    int index = 0;
    while (index < count) {
        if ((data[index] & 1) == 0) {
            total = total + data[index];
        }
        index = index + 1;
    }
    return total;
}

int KRSumPositive(const int* data, int count) {
    int total = 0;
    int index = 0;
    while (index < count) {
        if (data[index] > 0) {
            total = total + data[index];
        }
        index = index + 1;
    }
    return total;
}

void KRAbsArray(int* data, int count) {
    int index = 0;
    while (index < count) {
        data[index] = KRAbs(data[index]);
        index = index + 1;
    }
}

void KRPrefixSums(int* data, int count) {
    int index = 1;
    while (index < count) {
        data[index] = data[index] + data[index - 1];
        index = index + 1;
    }
}

int KRDotProduct(const int* left, const int* right, int count) {
    int total = 0;
    int index = 0;
    while (index < count) {
        total = total + left[index] * right[index];
        index = index + 1;
    }
    return total;
}

int KRMismatch(const int* left, const int* right, int count) {
    int index = 0;
    while (index < count) {
        if (left[index] != right[index]) {
            return index;
        }
        index = index + 1;
    }
    return -1;
}

int KRMergeTwo(const int* left, int left_count, const int* right, int right_count, int* out, int capacity) {
    int a = 0;
    int b = 0;
    int out_index = 0;
    while (a < left_count && b < right_count && out_index < capacity) {
        if (left[a] <= right[b]) {
            out[out_index] = left[a];
            a = a + 1;
        } else {
            out[out_index] = right[b];
            b = b + 1;
        }
        out_index = out_index + 1;
    }
    while (a < left_count && out_index < capacity) {
        out[out_index] = left[a];
        a = a + 1;
        out_index = out_index + 1;
    }
    while (b < right_count && out_index < capacity) {
        out[out_index] = right[b];
        b = b + 1;
        out_index = out_index + 1;
    }
    return out_index;
}

// ---------------------------------------------------------------- 6 statistics

double KRMean(const int* data, int count) {
    if (count <= 0) {
        return 0.0;
    }
    double total = 0.0;
    int index = 0;
    while (index < count) {
        total = total + (double)data[index];
        index = index + 1;
    }
    return total / (double)count;
}

double KRVariance(const int* data, int count) {
    if (count <= 0) {
        return 0.0;
    }
    double mean = KRMean(data, count);
    double total = 0.0;
    int index = 0;
    while (index < count) {
        double delta = (double)data[index] - mean;
        total = total + delta * delta;
        index = index + 1;
    }
    return total / (double)count;
}

double KRStdDev(const int* data, int count) {
    double variance = KRVariance(data, count);
    double guess = variance;
    if (guess <= 0.0) {
        return 0.0;
    }
    int step = 0;
    while (step < 24) {
        guess = 0.5 * (guess + variance / guess);
        step = step + 1;
    }
    return guess;
}

int KRMedianSorted(const int* data, int count) {
    if (count <= 0) {
        return 0;
    }
    if (count % 2 == 1) {
        return data[count / 2];
    }
    return (data[count / 2 - 1] + data[count / 2]) / 2;
}

int KRRangeValue(const int* data, int count) {
    if (count <= 0) {
        return 0;
    }
    return KRMaxValue(data, count) - KRMinValue(data, count);
}

int KRPercentileSorted(const int* data, int count, int percent) {
    if (count <= 0) {
        return 0;
    }
    int position = percent * (count - 1) / 100;
    if (position < 0) {
        position = 0;
    }
    if (position >= count) {
        position = count - 1;
    }
    return data[position];
}

int KRMovingSum(const int* data, int count, int width) {
    if (width <= 0 || width > count) {
        return 0;
    }
    int total = 0;
    int index = 0;
    while (index < width) {
        total = total + data[index];
        index = index + 1;
    }
    int best = total;
    while (index < count) {
        total = total + data[index] - data[index - width];
        if (total > best) {
            best = total;
        }
        index = index + 1;
    }
    return best;
}

int KRLongestRun(const int* data, int count) {
    if (count <= 0) {
        return 0;
    }
    int best = 1;
    int current = 1;
    int index = 1;
    while (index < count) {
        if (data[index] == data[index - 1]) {
            current = current + 1;
            if (current > best) {
                best = current;
            }
        } else {
            current = 1;
        }
        index = index + 1;
    }
    return best;
}

int KRLongestIncreasing(const int* data, int count) {
    if (count <= 0) {
        return 0;
    }
    int best = 1;
    int current = 1;
    int index = 1;
    while (index < count) {
        if (data[index] > data[index - 1]) {
            current = current + 1;
            if (current > best) {
                best = current;
            }
        } else {
            current = 1;
        }
        index = index + 1;
    }
    return best;
}

int KRChecksum(const int* data, int count) {
    unsigned sum = 0;
    int index = 0;
    while (index < count) {
        sum = sum + (unsigned)data[index];
        sum = (sum << 1) | (sum >> 31);
        index = index + 1;
    }
    return (int)sum;
}

// ---------------------------------------------------------------- 7 containers

struct KRIntStack {
    int items[32];
    int count;

    KRIntStack() : count(0) {}

    int push(int value) {
        if (count >= 32) {
            return 0;
        }
        items[count] = value;
        count = count + 1;
        return 1;
    }

    int pop(int* value) {
        if (count <= 0) {
            return 0;
        }
        count = count - 1;
        *value = items[count];
        return 1;
    }

    int peek() const {
        if (count <= 0) {
            return 0;
        }
        return items[count - 1];
    }

    int size() const {
        return count;
    }

    int empty() const {
        return count == 0;
    }

    void clear() {
        count = 0;
    }

    int sum() const {
        int total = 0;
        int index = 0;
        while (index < count) {
            total = total + items[index];
            index = index + 1;
        }
        return total;
    }
};

int KRIntStackRoundTrip(int limit) {
    KRIntStack stack;
    int index = 0;
    while (index < limit) {
        stack.push(index * 3);
        index = index + 1;
    }
    int total = 0;
    int value = 0;
    while (stack.pop(&value) != 0) {
        total = total + value;
    }
    return total + stack.size();
}

struct KRIntQueue {
    int items[24];
    int head;
    int tail;
    int count;

    KRIntQueue() : head(0), tail(0), count(0) {}

    int push(int value) {
        if (count >= 24) {
            return 0;
        }
        items[tail] = value;
        tail = (tail + 1) % 24;
        count = count + 1;
        return 1;
    }

    int pop(int* value) {
        if (count <= 0) {
            return 0;
        }
        *value = items[head];
        head = (head + 1) % 24;
        count = count - 1;
        return 1;
    }

    int size() const {
        return count;
    }

    int peek() const {
        if (count <= 0) {
            return 0;
        }
        return items[head];
    }
};

int KRIntQueueDrain(int count) {
    KRIntQueue queue;
    int index = 0;
    while (index < count) {
        queue.push(index + 1);
        index = index + 1;
    }
    int total = 0;
    int value = 0;
    while (queue.pop(&value) != 0) {
        total = total + value * 2;
    }
    return total;
}

class KRIntVector {
public:
    KRIntVector() : data_(nullptr), count_(0), capacity_(0) {}

    KRIntVector(int capacity) : data_(nullptr), count_(0), capacity_(0) {
        reserve(capacity);
    }

    KRIntVector(const KRIntVector& other) : data_(nullptr), count_(0), capacity_(0) {
        reserve(other.count_);
        int index = 0;
        while (index < other.count_) {
            data_[index] = other.data_[index];
            index = index + 1;
        }
        count_ = other.count_;
    }

    ~KRIntVector() {
        delete[] data_;
        data_ = nullptr;
        count_ = 0;
        capacity_ = 0;
    }

    KRIntVector& operator=(const KRIntVector& other) {
        if (this == &other) {
            return *this;
        }
        reserve(other.count_);
        int index = 0;
        while (index < other.count_) {
            data_[index] = other.data_[index];
            index = index + 1;
        }
        count_ = other.count_;
        return *this;
    }

    int reserve(int wanted) {
        if (wanted <= capacity_) {
            return 1;
        }
        int grown = capacity_ == 0 ? 4 : capacity_;
        while (grown < wanted) {
            grown = grown * 2;
        }
        int* fresh = new int[grown];
        if (fresh == nullptr) {
            return 0;
        }
        int index = 0;
        while (index < count_) {
            fresh[index] = data_[index];
            index = index + 1;
        }
        delete[] data_;
        data_ = fresh;
        capacity_ = grown;
        return 1;
    }

    int push(int value) {
        if (count_ == capacity_) {
            if (reserve(count_ + 1) == 0) {
                return 0;
            }
        }
        data_[count_] = value;
        count_ = count_ + 1;
        return 1;
    }

    int pop(int* value) {
        if (count_ <= 0) {
            return 0;
        }
        count_ = count_ - 1;
        *value = data_[count_];
        return 1;
    }

    int& operator[](int index) {
        return data_[index];
    }

    int at(int index) const {
        if (index < 0 || index >= count_) {
            return 0;
        }
        return data_[index];
    }

    int size() const {
        return count_;
    }

    int capacity() const {
        return capacity_;
    }

    void clear() {
        count_ = 0;
    }

    int sum() const {
        int total = 0;
        int index = 0;
        while (index < count_) {
            total = total + data_[index];
            index = index + 1;
        }
        return total;
    }

private:
    int* data_;
    int count_;
    int capacity_;
};

int KRIntVectorExercise(int count) {
    KRIntVector values;
    int index = 0;
    while (index < count) {
        values.push(index * index);
        index = index + 1;
    }
    values[0] = values[0] + 1;
    KRIntVector copied(values);
    int total = values.sum() + copied.sum();
    copied.clear();
    copied.push(7);
    return total + copied.at(0) + copied.size() + values.capacity();
}

class KRText {
public:
    KRText() : length_(0) {
        buffer_[0] = 0;
    }

    KRText(const char* text) : length_(0) {
        assign(text);
    }

    int assign(const char* text) {
        length_ = 0;
        while (text[length_] != 0 && length_ < 63) {
            buffer_[length_] = text[length_];
            length_ = length_ + 1;
        }
        buffer_[length_] = 0;
        return length_;
    }

    int append(const char* text) {
        int index = 0;
        while (text[index] != 0 && length_ < 63) {
            buffer_[length_] = text[index];
            length_ = length_ + 1;
            index = index + 1;
        }
        buffer_[length_] = 0;
        return length_;
    }

    int appendChar(char value) {
        if (length_ >= 63) {
            return length_;
        }
        buffer_[length_] = value;
        length_ = length_ + 1;
        buffer_[length_] = 0;
        return length_;
    }

    const char* text() const {
        return buffer_;
    }

    int length() const {
        return length_;
    }

    char at(int index) const {
        if (index < 0 || index >= length_) {
            return 0;
        }
        return buffer_[index];
    }

    char& operator[](int index) {
        return buffer_[index];
    }

    int equals(const KRText& other) const {
        if (length_ != other.length_) {
            return 0;
        }
        int index = 0;
        while (index < length_) {
            if (buffer_[index] != other.buffer_[index]) {
                return 0;
            }
            index = index + 1;
        }
        return 1;
    }

    int compare(const KRText& other) const {
        int index = 0;
        while (index < length_ && index < other.length_) {
            if (buffer_[index] != other.buffer_[index]) {
                return (int)buffer_[index] - (int)other.buffer_[index];
            }
            index = index + 1;
        }
        return length_ - other.length_;
    }

private:
    char buffer_[64];
    int length_;
};

int KRTextCombine(int seed) {
    KRText text("kr32");
    text.append("-libcpp");
    text.appendChar((char)('0' + seed % 10));
    KRText other(text);
    return text.length() + (text.equals(other) ? 1 : 0) + (int)text[0];
}

struct KRVec2 {
    float x;
    float y;

    KRVec2() : x(0.0f), y(0.0f) {}
    KRVec2(float a, float b) : x(a), y(b) {}

    KRVec2 operator+(const KRVec2& other) const {
        return KRVec2(x + other.x, y + other.y);
    }

    KRVec2 operator-(const KRVec2& other) const {
        return KRVec2(x - other.x, y - other.y);
    }

    KRVec2 operator*(float scale) const {
        return KRVec2(x * scale, y * scale);
    }

    KRVec2& operator+=(const KRVec2& other) {
        x = x + other.x;
        y = y + other.y;
        return *this;
    }

    bool operator==(const KRVec2& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const KRVec2& other) const {
        return x != other.x || y != other.y;
    }

    float dot(const KRVec2& other) const {
        return x * other.x + y * other.y;
    }

    float lengthSquared() const {
        return x * x + y * y;
    }
};

KRVec2 KRLerpVec2(const KRVec2& a, const KRVec2& b, float t) {
    KRVec2 delta(b.x - a.x, b.y - a.y);
    return KRVec2(a.x + delta.x * t, a.y + delta.y * t);
}

float KRVec2Area(const KRVec2& a, const KRVec2& b) {
    return (b.x - a.x) * (b.y - a.y);
}

struct KRMat3 {
    float m[9];

    KRMat3() {
        int index = 0;
        while (index < 9) {
            m[index] = 0.0f;
            index = index + 1;
        }
    }

    static KRMat3 identity() {
        KRMat3 result;
        result.m[0] = 1.0f;
        result.m[4] = 1.0f;
        result.m[8] = 1.0f;
        return result;
    }

    float at(int row, int column) const {
        return m[row * 3 + column];
    }

    void set(int row, int column, float value) {
        m[row * 3 + column] = value;
    }

    KRMat3 operator*(const KRMat3& other) const {
        KRMat3 result;
        int row = 0;
        while (row < 3) {
            int column = 0;
            while (column < 3) {
                float total = 0.0f;
                int step = 0;
                while (step < 3) {
                    total = total + at(row, step) * other.at(step, column);
                    step = step + 1;
                }
                result.set(row, column, total);
                column = column + 1;
            }
            row = row + 1;
        }
        return result;
    }

    float trace() const {
        return m[0] + m[4] + m[8];
    }

    float determinant() const {
        float a = m[0];
        float b = m[1];
        float c = m[2];
        float d = m[3];
        float e = m[4];
        float f = m[5];
        float g = m[6];
        float h = m[7];
        float i = m[8];
        return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    }

    KRMat3 transposed() const {
        KRMat3 result;
        int row = 0;
        while (row < 3) {
            int column = 0;
            while (column < 3) {
                result.set(row, column, at(column, row));
                column = column + 1;
            }
            row = row + 1;
        }
        return result;
    }
};

KRMat3 KRMat3Scale(float sx, float sy, float sz) {
    KRMat3 result = KRMat3::identity();
    result.set(0, 0, sx);
    result.set(1, 1, sy);
    result.set(2, 2, sz);
    return result;
}

float KRMat3Exercise(float scale) {
    KRMat3 rotation = KRMat3::identity();
    rotation.set(0, 1, -scale);
    rotation.set(1, 0, scale);
    KRMat3 scaled = KRMat3Scale(scale, scale, 1.0f);
    KRMat3 product = rotation * scaled;
    KRMat3 flipped = product.transposed();
    return flipped.trace() + flipped.determinant();
}

class KRBitSet {
public:
    KRBitSet() {
        int index = 0;
        while (index < 4) {
            words_[index] = 0;
            index = index + 1;
        }
    }

    void set(int bit) {
        if (bit < 0 || bit >= 128) {
            return;
        }
        words_[bit >> 5] = words_[bit >> 5] | (1u << (bit & 31));
    }

    void clear(int bit) {
        if (bit < 0 || bit >= 128) {
            return;
        }
        words_[bit >> 5] = words_[bit >> 5] & ~(1u << (bit & 31));
    }

    int test(int bit) const {
        if (bit < 0 || bit >= 128) {
            return 0;
        }
        return (words_[bit >> 5] & (1u << (bit & 31))) != 0;
    }

    int count() const {
        int total = 0;
        int index = 0;
        while (index < 4) {
            total = total + KRPopCount(words_[index]);
            index = index + 1;
        }
        return total;
    }

    void merge(const KRBitSet& other) {
        int index = 0;
        while (index < 4) {
            words_[index] = words_[index] | other.words_[index];
            index = index + 1;
        }
    }

    void intersect(const KRBitSet& other) {
        int index = 0;
        while (index < 4) {
            words_[index] = words_[index] & other.words_[index];
            index = index + 1;
        }
    }

    int any() const {
        int index = 0;
        while (index < 4) {
            if (words_[index] != 0) {
                return 1;
            }
            index = index + 1;
        }
        return 0;
    }

private:
    unsigned words_[4];
};

int KRBitSetExercise(int limit) {
    KRBitSet evens;
    KRBitSet threes;
    int index = 0;
    while (index < limit) {
        if (index % 2 == 0) {
            evens.set(index);
        }
        if (index % 3 == 0) {
            threes.set(index);
        }
        index = index + 1;
    }
    evens.intersect(threes);
    return evens.count() + (evens.any() != 0 ? 1 : 0);
}

// ---------------------------------------------------------------- 8 inheritance

class KRShape {
public:
    KRShape() : tag_(0) {}
    virtual ~KRShape() {}

    virtual float area() const = 0;
    virtual float perimeter() const = 0;
    virtual int sides() const {
        return 0;
    }

    void setTag(int tag) {
        tag_ = tag;
    }

    int tag() const {
        return tag_;
    }

protected:
    int tag_;
};

class KRRectangle : public KRShape {
public:
    KRRectangle(float width, float height) : width_(width), height_(height) {}

    float area() const override {
        return width_ * height_;
    }

    float perimeter() const override {
        return 2.0f * (width_ + height_);
    }

    int sides() const override {
        return 4;
    }

    float diagonal() const {
        return width_ * width_ + height_ * height_;
    }

private:
    float width_;
    float height_;
};

class KRTriangle : public KRShape {
public:
    KRTriangle(float a, float b, float c) : a_(a), b_(b), c_(c) {}

    float area() const override {
        float s = (a_ + b_ + c_) * 0.5f;
        float product = s * (s - a_) * (s - b_) * (s - c_);
        if (product <= 0.0f) {
            return 0.0f;
        }
        float guess = product * 0.5f;
        int step = 0;
        while (step < 20) {
            guess = 0.5f * (guess + product / guess);
            step = step + 1;
        }
        return guess;
    }

    float perimeter() const override {
        return a_ + b_ + c_;
    }

    int sides() const override {
        return 3;
    }

private:
    float a_;
    float b_;
    float c_;
};

class KRCircle : public KRShape {
public:
    KRCircle(float radius) : radius_(radius) {}

    float area() const override {
        return 3.14159265f * radius_ * radius_;
    }

    float perimeter() const override {
        return 2.0f * 3.14159265f * radius_;
    }

    int sides() const override {
        return 1;
    }

private:
    float radius_;
};

float KRShapeArea(const KRShape* shape) {
    if (shape == nullptr) {
        return 0.0f;
    }
    return shape->area();
}

float KRShapeTotalArea(KRShape** shapes, int count) {
    float total = 0.0f;
    int index = 0;
    while (index < count) {
        total = total + KRShapeArea(shapes[index]);
        index = index + 1;
    }
    return total;
}

int KRShapeTotalSides(KRShape** shapes, int count) {
    int total = 0;
    int index = 0;
    while (index < count) {
        total = total + shapes[index]->sides();
        index = index + 1;
    }
    return total;
}

class KRAccount {
public:
    KRAccount(int balance) : balance_(balance) {}
    virtual ~KRAccount() {}

    virtual int withdraw(int amount) {
        if (amount > balance_) {
            return 0;
        }
        balance_ = balance_ - amount;
        return amount;
    }

    virtual int fee() const {
        return 1;
    }

    int balance() const {
        return balance_;
    }

protected:
    int balance_;
};

class KRSavings : public KRAccount {
public:
    KRSavings(int balance, int rate) : KRAccount(balance), rate_(rate) {}

    int withdraw(int amount) override {
        int charged = amount + fee();
        if (charged > balance()) {
            return 0;
        }
        balance_ = balance_ - charged;
        return amount;
    }

    int fee() const override {
        return 2;
    }

    int interest() const {
        return balance_ * rate_ / 100;
    }

private:
    int rate_;
};

class KRChecking : public KRAccount {
public:
    KRChecking(int balance, int overdraft) : KRAccount(balance), overdraft_(overdraft) {}

    int withdraw(int amount) override {
        if (amount > balance_ + overdraft_) {
            return 0;
        }
        balance_ = balance_ - amount;
        return amount;
    }

    int fee() const override {
        return 0;
    }

private:
    int overdraft_;
};

int KRBankExercise(int amount) {
    KRSavings savings(1000, 5);
    KRChecking checking(200, 50);
    KRAccount* accounts[2];
    accounts[0] = &savings;
    accounts[1] = &checking;
    int total = 0;
    int index = 0;
    while (index < 2) {
        total = total + accounts[index]->withdraw(amount);
        total = total + accounts[index]->fee();
        index = index + 1;
    }
    return total + savings.interest() + checking.balance();
}

// ---------------------------------------------------------------- 9 templates

template <typename T>
T KRMaxOf(T left, T right) {
    if (left > right) {
        return left;
    }
    return right;
}

template <typename T>
T KRMinOf(T left, T right) {
    if (left < right) {
        return left;
    }
    return right;
}

template <typename T>
T KRClampOf(T value, T low, T high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

template <typename T>
void KRSwapOf(T* left, T* right) {
    T temporary = *left;
    *left = *right;
    *right = temporary;
}

template <typename T>
struct KRPair {
    T first;
    T second;

    KRPair() : first(), second() {}
    KRPair(T a, T b) : first(a), second(b) {}

    T sum() const {
        return first + second;
    }

    void swap() {
        T temporary = first;
        first = second;
        second = temporary;
    }
};

template <typename T, int Capacity>
struct KRFixedArray {
    T items[Capacity];
    int count;

    KRFixedArray() : count(0) {}

    int push(T value) {
        if (count >= Capacity) {
            return 0;
        }
        items[count] = value;
        count = count + 1;
        return 1;
    }

    T total() const {
        T sum = T();
        int index = 0;
        while (index < count) {
            sum = sum + items[index];
            index = index + 1;
        }
        return sum;
    }

    T largest() const {
        T best = items[0];
        int index = 1;
        while (index < count) {
            if (items[index] > best) {
                best = items[index];
            }
            index = index + 1;
        }
        return best;
    }
};

template <typename T>
T KRAccumulate(const T* data, int count) {
    T total = T();
    int index = 0;
    while (index < count) {
        total = total + data[index];
        index = index + 1;
    }
    return total;
}

template <typename T>
int KRIndexOf(const T* data, int count, T wanted) {
    int index = 0;
    while (index < count) {
        if (data[index] == wanted) {
            return index;
        }
        index = index + 1;
    }
    return -1;
}

template <typename T>
void KRReverseOf(T* data, int count) {
    int left = 0;
    int right = count - 1;
    while (left < right) {
        KRSwapOf(data + left, data + right);
        left = left + 1;
        right = right - 1;
    }
}

long long KRTemplateExercise(int limit) {
    KRPair<int> pair(3, 4);
    pair.swap();
    KRFixedArray<int, 8> boxed;
    int index = 0;
    while (index < limit && index < 8) {
        boxed.push(index * 5);
        index = index + 1;
    }
    long long data[6] = {3, 1, 4, 1, 5, 9};
    long long total = KRAccumulate(data, 6);
    long long biggest = KRMaxOf(KRMinOf(4LL, 9LL), total);
    return pair.sum() + boxed.total() + boxed.largest() + (int)biggest;
}

// ---------------------------------------------------------------- 10 floating point

double KRSqrtNewton(double value) {
    if (value <= 0.0) {
        return 0.0;
    }
    double guess = value * 0.5;
    int step = 0;
    while (step < 32) {
        double next = 0.5 * (guess + value / guess);
        if (next == guess) {
            return next;
        }
        guess = next;
        step = step + 1;
    }
    return guess;
}

double KRSinTaylor(double radians) {
    double term = radians;
    double total = radians;
    int step = 1;
    while (step < 12) {
        double denominator = (double)((2 * step) * (2 * step + 1));
        term = -term * radians * radians / denominator;
        total = total + term;
        step = step + 1;
    }
    return total;
}

double KRCosTaylor(double radians) {
    double term = 1.0;
    double total = 1.0;
    int step = 1;
    while (step < 12) {
        double denominator = (double)((2 * step - 1) * (2 * step));
        term = -term * radians * radians / denominator;
        total = total + term;
        step = step + 1;
    }
    return total;
}

double KRExpTaylor(double value) {
    if (value < 0.0) {
        return 1.0 / KRExpTaylor(-value);
    }
    double term = 1.0;
    double total = 1.0;
    int step = 1;
    while (step < 20) {
        term = term * value / (double)step;
        total = total + term;
        step = step + 1;
    }
    return total;
}

double KRLogNewton(double value) {
    if (value <= 0.0) {
        return 0.0;
    }
    double guess = value;
    int step = 0;
    while (step < 32) {
        double exponential = KRExpTaylor(guess);
        double next = guess + (value - exponential) / exponential;
        if (next == guess) {
            return next;
        }
        guess = next;
        step = step + 1;
    }
    return guess;
}

double KRPowDouble(double base, int exponent) {
    double result = 1.0;
    double factor = base;
    int remaining = exponent;
    if (remaining < 0) {
        remaining = -remaining;
        factor = 1.0 / factor;
    }
    while (remaining > 0) {
        if ((remaining & 1) != 0) {
            result = result * factor;
        }
        factor = factor * factor;
        remaining = remaining >> 1;
    }
    return result;
}

double KRHypot(double x, double y) {
    double ax = x < 0.0 ? -x : x;
    double ay = y < 0.0 ? -y : y;
    if (ax < ay) {
        double temporary = ax;
        ax = ay;
        ay = temporary;
    }
    if (ax == 0.0) {
        return 0.0;
    }
    double ratio = ay / ax;
    return ax * KRSqrtNewton(1.0 + ratio * ratio);
}

double KRLerpDouble(double a, double b, double t) {
    return a + (b - a) * t;
}

double KRToRadians(double degrees) {
    return degrees * 3.14159265358979 / 180.0;
}

double KRToDegrees(double radians) {
    return radians * 180.0 / 3.14159265358979;
}

float KRSqrtFloat(float value) {
    if (value <= 0.0f) {
        return 0.0f;
    }
    float guess = value * 0.5f;
    int step = 0;
    while (step < 24) {
        guess = 0.5f * (guess + value / guess);
        step = step + 1;
    }
    return guess;
}

float KRFloorFloat(float value) {
    float truncated = (float)(int)value;
    if (truncated > value) {
        return truncated - 1.0f;
    }
    return truncated;
}

float KRCeilFloat(float value) {
    float truncated = (float)(int)value;
    if (truncated < value) {
        return truncated + 1.0f;
    }
    return truncated;
}

float KRRoundFloat(float value) {
    if (value < 0.0f) {
        return -KRFloorFloat(-value + 0.5f);
    }
    return KRFloorFloat(value + 0.5f);
}

float KRFma(float a, float b, float c) {
    return a * b + c;
}

unsigned KRFloatBits(float value) {
    float* pointer = &value;
    unsigned* bits = (unsigned*)pointer;
    return *bits;
}

float KRBitsFloat(unsigned bits) {
    unsigned* pointer = &bits;
    float* value = (float*)pointer;
    return *value;
}

int KRIsFinite(double value) {
    if (value != value) {
        return 0;
    }
    if (value > 1.0e308) {
        return 0;
    }
    if (value < -1.0e308) {
        return 0;
    }
    return 1;
}

double KRGoldenRatioError(int iterations) {
    double a = 1.0;
    double b = 1.0;
    int step = 0;
    while (step < iterations) {
        double next = a + b;
        a = b;
        b = next;
        step = step + 1;
    }
    if (b == 0.0) {
        return 0.0;
    }
    return a / b;
}

float KRFloatExercise(float radius) {
    float area = 3.14159265f * radius * radius;
    float root = KRSqrtFloat(area);
    float rounded = KRRoundFloat(root);
    float clamped = KRClamp(root - rounded, -1.0f, 1.0f);
    return KRFma(clamped, 2.0f, KRCeilFloat(area) - KRFloorFloat(area));
}

// ---------------------------------------------------------------- 11 errors

class KRGuard {
public:
    explicit KRGuard(int* target) : target_(target) {}
    ~KRGuard() {
        if (target_ != nullptr) {
            *target_ = *target_ + 1;
        }
    }

    void release() {
        target_ = nullptr;
    }

private:
    int* target_;
};

int KRCheckedDiv(int numerator, int denominator) {
    if (denominator == 0) {
        throw 1;
    }
    return numerator / denominator;
}

int KRCheckedSub(int left, int right) {
    if (right > 0 && left < -2147483647 + right) {
        throw 2;
    }
    if (right < 0 && left > 2147483647 + right) {
        throw 3;
    }
    return left - right;
}

int KRValidate(int value) {
    if (value < 0) {
        throw value;
    }
    if (value > 100) {
        throw 100;
    }
    return value;
}

int KRTryCompute(int a, int b) {
    int result = 0;
    try {
        result = KRCheckedDiv(a, b);
        result = result + KRValidate(result);
    } catch (int code) {
        result = -code;
    }
    return result;
}

int KRGuardExercise(int rounds) {
    int releases = 0;
    int index = 0;
    while (index < rounds) {
        KRGuard guard(&releases);
        if (index % 3 == 0) {
            guard.release();
        }
        index = index + 1;
    }
    return releases;
}

int KRExceptionMatrix(int value) {
    int total = 0;
    int index = 0;
    while (index < 4) {
        try {
            total = total + KRCheckedDiv(value, index);
        } catch (int code) {
            total = total + code * 10;
        }
        index = index + 1;
    }
    return total;
}

// ---------------------------------------------------------------- 12 hashing

unsigned KRFnv1a(const void* block, int count) {
    const unsigned char* bytes = (const unsigned char*)block;
    unsigned hash = 2166136261u;
    int index = 0;
    while (index < count) {
        hash = hash ^ (unsigned)bytes[index];
        hash = hash * 16777619u;
        index = index + 1;
    }
    return hash;
}

unsigned KRAdler32(const void* block, int count) {
    const unsigned char* bytes = (const unsigned char*)block;
    unsigned a = 1;
    unsigned b = 0;
    int index = 0;
    while (index < count) {
        a = (a + (unsigned)bytes[index]) % 65521u;
        b = (b + a) % 65521u;
        index = index + 1;
    }
    return (b << 16) | a;
}

unsigned KRCrc32(const void* block, int count) {
    const unsigned char* bytes = (const unsigned char*)block;
    unsigned crc = 0xFFFFFFFFu;
    int index = 0;
    while (index < count) {
        crc = crc ^ (unsigned)bytes[index];
        int bit = 0;
        while (bit < 8) {
            if ((crc & 1u) != 0) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc = crc >> 1;
            }
            bit = bit + 1;
        }
        index = index + 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

unsigned KRMix32(unsigned value) {
    unsigned mixed = value;
    mixed = mixed ^ (mixed >> 16);
    mixed = mixed * 0x7FEB352Du;
    mixed = mixed ^ (mixed >> 15);
    mixed = mixed * 0x846CA68Bu;
    mixed = mixed ^ (mixed >> 16);
    return mixed;
}

unsigned long long KRMix64(unsigned long long value) {
    unsigned long long mixed = value;
    mixed = mixed ^ (mixed >> 33);
    mixed = mixed * 0xFF51AFD7ED558CCDu;
    mixed = mixed ^ (mixed >> 33);
    mixed = mixed * 0xC4CEB9FE1A85EC53u;
    mixed = mixed ^ (mixed >> 33);
    return mixed;
}

unsigned KRHashInts(const int* data, int count) {
    unsigned hash = 0x9E3779B9u;
    int index = 0;
    while (index < count) {
        hash = hash ^ (unsigned)data[index];
        hash = KRMix32(hash);
        index = index + 1;
    }
    return hash;
}

int KRHistogram(const int* data, int count, int* buckets, int bucket_count) {
    int index = 0;
    while (index < bucket_count) {
        buckets[index] = 0;
        index = index + 1;
    }
    index = 0;
    while (index < count) {
        int bucket = data[index] % bucket_count;
        if (bucket < 0) {
            bucket = bucket + bucket_count;
        }
        buckets[bucket] = buckets[bucket] + 1;
        index = index + 1;
    }
    int widest = 0;
    index = 1;
    while (index < bucket_count) {
        if (buckets[index] > buckets[widest]) {
            widest = index;
        }
        index = index + 1;
    }
    return widest;
}

unsigned KRHashCombine(unsigned seed, unsigned value) {
    return seed ^ (value + 0x9E3779B9u + (seed << 6) + (seed >> 2));
}

int KRBloomProbe(unsigned hash, int bits) {
    int first = (int)(hash % (unsigned)bits);
    int second = (int)((hash >> 16) % (unsigned)bits);
    return (1 << (first & 31)) | (1 << (second & 31));
}

unsigned KRChecksumBytes(const void* block, int count) {
    const unsigned char* bytes = (const unsigned char*)block;
    unsigned total = 0;
    int index = 0;
    while (index < count) {
        total = total + (unsigned)bytes[index] * (unsigned)(index + 1);
        index = index + 1;
    }
    return total;
}

long long KRHashExercise(int limit) {
    int data[16];
    int index = 0;
    while (index < 16) {
        data[index] = index * 37 + limit;
        index = index + 1;
    }
    unsigned hash = KRHashInts(data, 16);
    unsigned crc = KRCrc32(data, 64);
    unsigned adler = KRAdler32(data, 64);
    unsigned long long wide = KRMix64((unsigned long long)hash << 32 | crc);
    int buckets[5];
    int widest = KRHistogram(data, 16, buckets, 5);
    return (long long)(wide & 0xFFFF) + (long long)adler + widest;
}
