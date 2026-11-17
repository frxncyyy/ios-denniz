#pragma once
#include <stddef.h>
#include <sys/types.h>
int close(int);
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
