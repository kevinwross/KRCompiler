#include <stdio.h>

#include "console.hpp"

Console::Console() {
    script_ = nullptr;
    scriptLength_ = 0;
    scriptPosition_ = 0;
}

void Console::setScript(const char* text, int length) {
    if (length < 0) {
        length = 0;
    }
    if (length > 4096) {
        length = 4096;
    }
    script_ = text;
    scriptLength_ = length;
    scriptPosition_ = 0;
}

void Console::text(const char* value) {
    printf("%s", value);
}

void Console::line(const char* value) {
    printf("%s\n", value);
}

void Console::prompt(const char* value) {
    printf("%s", value);
}

bool Console::readLine(char* buffer, int capacity) {
    if (script_ != nullptr) {
        if (scriptPosition_ >= scriptLength_) {
            buffer[0] = 0;
            return false;
        }
        int count = 0;
        while (scriptPosition_ < scriptLength_) {
            const char character = script_[scriptPosition_];
            if (character == '\n' || character == '\r') {
                break;
            }
            if (count + 1 < capacity) {
                buffer[count] = character;
                count = count + 1;
            }
            scriptPosition_ = scriptPosition_ + 1;
        }
        if (scriptPosition_ < scriptLength_ && script_[scriptPosition_] == '\r') {
            scriptPosition_ = scriptPosition_ + 1;
        }
        if (scriptPosition_ < scriptLength_ && script_[scriptPosition_] == '\n') {
            scriptPosition_ = scriptPosition_ + 1;
        }
        buffer[count] = 0;
        return true;
    }
    int count = 0;
    int c = getchar();
    if (c < 0) {
        buffer[0] = 0;
        return false;
    }
    while (c >= 0 && c != '\n' && c != '\r') {
        if (count + 1 < capacity) {
            buffer[count] = (char)c;
            count = count + 1;
        }
        c = getchar();
    }
    buffer[count] = 0;
    return true;
}

bool Console::parseInteger(const char* text, int* value) const {
    while (*text == ' ' || *text == '\t') {
        text = text + 1;
    }
    int sign = 1;
    if (*text == '-') {
        sign = -1;
        text = text + 1;
    } else if (*text == '+') {
        text = text + 1;
    }
    if (*text < '0' || *text > '9') {
        return false;
    }
    int result = 0;
    while (*text >= '0' && *text <= '9') {
        result = result * 10 + (*text - '0');
        text = text + 1;
    }
    while (*text == ' ' || *text == '\t') {
        text = text + 1;
    }
    if (*text != 0) {
        return false;
    }
    *value = sign * result;
    return true;
}

bool Console::readNumber(const char* promptText, int low, int high, int* value) {
    printf("%s", promptText);
    char line[64];
    if (!readLine(line, 64)) {
        return false;
    }
    int parsed = 0;
    if (!parseInteger(line, &parsed) || parsed < low || parsed > high) {
        printf("Expected a number from %d to %d.\n", low, high);
        return false;
    }
    *value = parsed;
    return true;
}

int Console::lower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

bool Console::startsWith3(const char* text, const char* word) {
    for (int i = 0; i < 3; ++i) {
        if (text[i] == 0 || lower(text[i]) != lower(word[i])) {
            return false;
        }
    }
    return true;
}
