#!/bin/sh

set +x

clang -Wall -Wextra -std=c11 -o main main.c -liconv -lz

./main book.umd
