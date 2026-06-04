#!/bin/sh

set +x

clang -Wall -Wextra -o umd2txt umd2txt.c -liconv

./umd2txt > ./book.txt

clang -Wall -Wextra -o main main.c -liconv

./main
