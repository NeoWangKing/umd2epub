/*
 *  The structure of .umd file is likely below.
 *  
 *  Structure of file header:
 *      (4  bytes) fixed file head (0xde9a9b89)
 *      (5  bytes) invalid data
 *      (1  byte ) type of file (1: txt, 2: img)
 *      (2  bytes) invalid data
 *  Structure of metadata x9:
 *      (1  byte ) '#'
 *      (2  bytes) type id of metadata
 *          id type 类型
 *          ---------------------------------
 *          2  text 标题
 *          3  text 作者
 *          4  text 出版年
 *          5  text 出版月
 *          6  text 出版日
 *          7  text 类型
 *          8  text 出版社
 *          9  text 经销商
 *          11 int  未经压缩的内容总长度
 *      (1  byte ) invalid data
 *      (1  byte ) length of metadata
 *      (n  bytes) meta data (length - 5)
 *  Structure of chapter contents:
 *      (1  byte ) '#': separator
 *      (2  bytes) '0x0083': sign of chapter
 *      (11 bytes) invalid data
 *      (4  bytes) uint32_t: nums of chapters ((num - 9)/4)
 *      (4n bytes) uint32_t: Represents the offset at the beginning of the chapter
 *                           after the content of the article is decompressed.
 *  Structure of chapter titles:
 *      (1  byte ) '#': separator
 *      (2  bytes) '0x0084': sign of chapter titles
 *      (11 bytes) invalid data
 *      (4  bytes) total length of titles
 *      ----------------------------------------------------
 *      (1  bytes) length of chapter title
 *      (n  bytes) title
 *      ----------------------------------------------------
 *      (1  bytes) length of chapter title
 *      (n  bytes) title
 *      ----------------------------------------------------
 *      ...
 *      ----------------------------------------------------
 *  Structure of content:
 *      (1  byte ) '0x24': sign of content
 *      (4  bytes) Invalid data
 *      (4  bytes) length of content data
 *      (        ) content data (Zlib compressed)
 *  After reading the data of a chapter:
 *      When it comes with '0x0A', jump  6 bytes and read next chapter
 *      When it comes with '0xF1', jump 18 bytes and read next chapter
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <iconv.h>
#include <string.h>
#include <zlib.h>

char *args_shift(int *argc, char ***argv)
{
    assert(*argc > 0);
    char *result = **argv;
    (*argc) -= 1;
    (*argv) += 1;
    return result;
}

unsigned char *generate_buffer(const char* file_path, size_t *bytesRead)
{
    FILE *book = NULL;

    book = fopen(file_path, "rb");
    if (book == NULL) {
        fprintf(stderr, "[ERROR] cannot open file %s", file_path);
        return NULL;
    }

    fseek(book, 0, SEEK_END);
    long file_size = ftell(book);
    fseek(book, 0, SEEK_SET);

    // 动态分配缓冲区
    unsigned char *buffer = malloc(file_size);
    if (!buffer) {
        fprintf(stderr, "[ERROR] error when malloc buffer %s", file_path);
        fclose(book);
        return NULL;
    }
    *bytesRead = fread(buffer, 1, file_size, book);
    if (bytesRead == 0) {
        fprintf(stderr, "[ERROR] cannot read file %s", file_path);
        fclose(book);
        return NULL;
    }
    fclose(book);
    return buffer;
}

uint32_t u32_from_buffer(const unsigned char *buffer, uint32_t *start, uint32_t len)
{
    uint32_t data = 0;
    if (len > 4) len = 4;
    for (size_t i = 0; i < len; ++i) {
        data |= (uint32_t)buffer[*start] << (i * 8);
        *start += 1;
    }
    return data;
}

void print_from_buffer(const unsigned char *buffer, uint32_t *start, uint32_t len)
{
    for (size_t i = 0; i < len; ++i) {
        printf("%02x ", buffer[*start]);
        *start += 1;
    }
    printf("\n");
}

bool print_next_block(const unsigned char *buffer, uint32_t *start, uint32_t buffer_len)
{
    if (*start >=buffer_len) {
        printf("[INFO] You have reached the end of the buffer");
        printf("\n\n");
        return false;
    }
    if (buffer[*start] == 0x23) {
        printf("[BLOCK] Func Block\n");
        printf("    flag: 0x23\n"); (*start)++;
        printf("    sign: 0x%04x\n", u32_from_buffer(buffer, start, 2));
        printf("    ");
        print_from_buffer(buffer, start, 1);
        uint32_t len = u32_from_buffer(buffer, start, 1);
        printf("    length: %u\n", len - 5);
        printf("    content: \n");
        printf("    ");
        for (size_t i = 0; i < len - 5; ++i) {
            printf("%02x ", buffer[*start]); (*start)++;
        }
        printf("\n\n");
        return true;
    }else if (buffer[*start] == 0x24) {
        printf("[BLOCK] Data Block\n");
        printf("    flag: 0x24\n"); (*start)++;
        printf("    sign: "); print_from_buffer(buffer, start, 4);
        uint32_t len = u32_from_buffer(buffer, start, 4);
        printf("    length: %u\n", len - 9);
        printf("    content: \n");
        printf("    ");
        for (size_t i = 0; i < len - 9; ++i) {
            printf("%02x ", buffer[*start]); (*start)++;
        }
        printf("\n\n");
        return true;
    }else {
        printf("[INFO] here is no a beginning of a block");
        for (size_t i = *start; i < *start + 100; ++i) {
            printf("%02x ", buffer[i]);
        }
        printf("\n\n");
        return false;
    }
    return false;
}

int main(int argc, char **argv)
{
    const char *program = args_shift(&argc, &argv);

    if (argc <= 0) {
        fprintf(stderr, "[Usage] %s <umd file>\n", program);
        fprintf(stderr, "[ERROR] no umd file is provided\n");
        return 1;
    }
    const char *file_path = args_shift(&argc, &argv);

    size_t bytesRead = 0;
    const unsigned char *buffer = generate_buffer(file_path, &bytesRead);
    uint32_t rp = 0;

    print_from_buffer(buffer, &rp, 4);
    printf("\n");

    bool reach_end = !print_next_block(buffer, &rp, bytesRead);

    while (!reach_end) {
        reach_end = !print_next_block(buffer, &rp, bytesRead);
    }

    return 0;
}
