#ifndef KRC_STRING_H
#define KRC_STRING_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
void *memchr(const void *buffer, int character, size_t count);
void *memccpy(void *destination, const void *source, int character, size_t count);
int memcmp(const void *left, const void *right, size_t count);

size_t strlen(const char *text);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t count);
char *strcat(char *destination, const char *source);
char *strncat(char *destination, const char *source, size_t count);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
int strcoll(const char *left, const char *right);
size_t strxfrm(char *destination, const char *source, size_t count);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
size_t strnlen(const char *text, size_t maximum);
char *strpbrk(const char *text, const char *accept);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strstr(const char *text, const char *needle);
char *strtok(char *text, const char *delimiters);
char *strtok_r(char *text, const char *delimiters, char **save);
char *strsep(char **text, const char *delimiters);
char *strdup(const char *text);
char *strndup(const char *text, size_t maximum);
char *strerror(int error);

#endif
