#ifndef STARTREKNG_SCRIPT_HPP
#define STARTREKNG_SCRIPT_HPP

/*
 * A deterministic session for the scripted build. Console caps any script at
 * 4096 bytes; this one exercises every command family and all six computer
 * reports.
 */
static const int kScriptCapacity = 4096;

static const char kScriptText[] =
    "srs\n"
    "lrs\n"
    "dam\n"
    "she\n"
    "100\n"
    "com\n"
    "0\n"
    "com\n"
    "1\n"
    "com\n"
    "2\n"
    "com\n"
    "3\n"
    "com\n"
    "4\n"
    "1\n"
    "2\n"
    "8\n"
    "7\n"
    "com\n"
    "5\n"
    "nav\n"
    "3\n"
    "1\n"
    "nav\n"
    "4\n"
    "2\n"
    "nav\n"
    "7\n"
    "1\n"
    "pha\n"
    "tor\n"
    "1\n"
    "tor\n"
    "3\n"
    "xxx\n";

static const int kScriptLength = sizeof(kScriptText) - 1;

#endif
