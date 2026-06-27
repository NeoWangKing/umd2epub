#!/bin/sh

set -x

clang -Wall -Wextra -std=c11 -o main main.c -lz

./main book.umd
