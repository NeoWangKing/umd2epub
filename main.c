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

typedef struct {
    uint32_t offset;
    uint32_t title;
    uint32_t title_len;
} Chapter;

// any encoding type -> UTF-8
char* convert_to_utf8(const unsigned char* data, size_t len, const char* enc_name)
{
    iconv_t cd = iconv_open("UTF-8", enc_name);
    if (cd == (iconv_t)-1) return NULL;

    size_t out_bytes = len * 2 + 1;
    char* result = malloc(out_bytes);
    if (!result) {
        iconv_close(cd);
        return NULL;
    }

    char* in_buf  = (char*)data;
    char* out_buf = result;
    size_t in_left = len;
    size_t out_left = out_bytes - 1;

    if (iconv(cd, &in_buf, &in_left, &out_buf, &out_left) == (size_t)-1) {
        free(result);
        iconv_close(cd);
        return NULL;
    }

    *out_buf = '\0';
    iconv_close(cd);
    return result;
}

// UTF-16LE -> UTF-8
char* utf16le_to_utf8(const unsigned char* data, size_t len)
{
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        data += 2;
        len  -= 2;
    }
    // 确保长度为偶数，若为奇数则丢弃最后一个孤立字节
    if (len & 1) len--;
    return convert_to_utf8(data, len, "UTF-16LE");
}

/**
 * 解压 zlib 数据，返回 malloc 的缓冲区，*out_len 为解压后长度。
 * 失败返回 NULL。
 */
unsigned char* zlib_decompress(const unsigned char* in, size_t in_len, size_t* out_len)
{
    z_stream strm = {0};
    int ret = inflateInit(&strm);
    if (ret != Z_OK) return NULL;

    const size_t CHUNK = 64 * 1024;  // 64 KB
    unsigned char* out = NULL;
    size_t total = 0;

    strm.next_in = (Bytef*)in;
    strm.avail_in = in_len;

    do {
        size_t new_size = total + CHUNK;
        unsigned char* tmp = realloc(out, new_size);
        if (!tmp) {
            free(out);
            inflateEnd(&strm);
            return NULL;
        }
        out = tmp;
        strm.avail_out = CHUNK;
        strm.next_out = out + total;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            free(out);
            inflateEnd(&strm);
            return NULL;
        }
        total = new_size - strm.avail_out;
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    *out_len = total;
    return out;
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

char *str_from_buffer(const unsigned char *buffer, uint32_t *start, uint32_t len)
{
    const unsigned char *data = &buffer[*start];
    char *str = utf16le_to_utf8(data, len);
    *start += len;
    return str;
}

unsigned char* read_from_buffer(const unsigned char *buffer, uint32_t *start, uint32_t len)
{
    unsigned char *out_buffer = malloc(len*sizeof(unsigned char));
    for (size_t i = 0; i < len; ++i) {
        out_buffer[i] = buffer[*start];
        *start += 1;
    }
    return out_buffer;
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
        return false;
    }
    if (buffer[*start] == 0x23) {
        printf("23\n"); (*start)++;
        print_from_buffer(buffer, start, 2);
        print_from_buffer(buffer, start, 1);
        uint32_t len = u32_from_buffer(buffer, start, 1); (*start)--;
        print_from_buffer(buffer, start, 1);
        for (size_t i = 0; i < len - 5; ++i) {
            printf("%02x ", buffer[*start]); (*start)++;
        }
        printf("\n");
        return true;
    }else if (buffer[*start] == 0x24) {
        printf("24\n"); (*start)++;
        print_from_buffer(buffer, start, 4);
        uint32_t len = u32_from_buffer(buffer, start, 4); (*start) -= 4;
        print_from_buffer(buffer, start, 4);
        for (size_t i = 0; i < len - 9; ++i) {
            printf("%02x ", buffer[*start]); (*start)++;
        }
        printf("\n");
        return true;
    }else {
        printf("[INFO] here is no a beginning of a block");
        for (size_t i = *start; i < *start + 100; ++i) {
            printf("%02x ", buffer[i]);
        }
        printf("\n");
        return false;
    }
    return false;
}

void read_contents(const unsigned char *buffer, uint32_t *rp, uint32_t bytesRead, unsigned char **full_text, size_t *full_text_len)
{
    while (*rp < bytesRead) {
        unsigned char flag = buffer[*rp];

        if (flag == 0x24) {
            // 读取一个正文数据块
            (*rp)++;  // 跳过 0x24

            // 跳过 4 字节无效数据
            read_from_buffer(buffer, rp, 4);

            // 读取块长度（包含 9 字节头部，所以内容长度 = length - 9）
            uint32_t block_length = u32_from_buffer(buffer, rp, 4);
            if (block_length < 9) {
                fprintf(stderr, "[ERROR] Invalid body block length %u\n", block_length);
                break;
            }
            uint32_t data_len = block_length - 9;

            // 读取压缩数据
            unsigned char *compressed = read_from_buffer(buffer, rp, data_len);
            if (!compressed) break;

            // zlib 解压
            size_t dec_len = 0;
            unsigned char *decompressed = zlib_decompress(compressed, data_len, &dec_len);
            free(compressed);

            if (!decompressed) {
                fprintf(stderr, "[ERROR] zlib decompress failed at rp=%u\n", *rp);
                break;
            }

            // 拼接到全文缓冲区
            unsigned char *tmp = realloc(*full_text, *full_text_len + dec_len);
            if (!tmp) {
                free(decompressed);
                break;
            }
            *full_text = tmp;
            memcpy(*full_text + *full_text_len, decompressed, dec_len);
            *full_text_len += dec_len;
            free(decompressed);

        } else if (flag == 0x23) {
            (*rp)++;
            unsigned char *flag2 = read_from_buffer(buffer, rp, 2);
            if (flag2[0] == 0x0A && flag2[1] == 0x00) {
                // 跳过 6 字节控制信息
                read_from_buffer(buffer, rp, 6);
            }else if (flag2[0] == 0xF1 && flag2[1] == 0x00) {
                // 跳过 18 字节控制信息
                read_from_buffer(buffer, rp, 18);
            } else {
                // 其他字符：正文结束，退出循环
                printf("[DEBUG] exiting loop, flag2=0x%02x%02x\n", flag2[1], flag2[0]);
                *rp -= 3;
                print_next_block(buffer, rp, bytesRead);
                break;
            }
        } else {
            // 其他字符：正文结束，退出循环
            printf("[DEBUG] exiting loop, flag=0x%02x\n", flag);
            print_next_block(buffer, rp, bytesRead);
            break;
        }
    }

    printf("[INFO] Decompressed total: %zu bytes\n", *full_text_len);
}

bool save_content_as(const unsigned char* buffer, const char *file_path, Chapter *chapters, uint32_t chapter_num, unsigned char *full_text, size_t full_text_len)
{
    if (full_text_len > 0) {
        // 跳过可能的 UTF‑16LE BOM
        if (full_text_len >= 2 && full_text[0] == 0xFF && full_text[1] == 0xFE) {
            memmove(full_text, full_text + 2, full_text_len - 2);
            full_text_len -= 2;
        }

        FILE *out = fopen(file_path, "w");
        if (!out) {
            fprintf(stderr, "[ERROR] Cannot write %s\n", file_path);
            return false;
        }

        for (size_t i = 0; i < chapter_num; i++) {
            // 计算本章在 full_text 中的起止字节偏移
            uint32_t start_off = chapters[i].offset;
            uint32_t end_off = (i + 1 < chapter_num) ? chapters[i+1].offset : (uint32_t)full_text_len;

            // 防止越界
            if (start_off >= full_text_len) break;
            if (end_off > full_text_len) end_off = full_text_len;
            if (end_off <= start_off) continue;

            // 提取本章的 UTF‑16LE 字节并转码
            uint32_t rp = chapters[i].title;
            char *chapter_title = str_from_buffer(buffer, &rp, chapters[i].title_len);
            char *chapter_utf8 = utf16le_to_utf8(full_text + start_off, end_off - start_off);
            if (chapter_utf8) {
                // fputs(chapter_title, out);
                // fputc('\n', out);
                fputs(chapter_utf8, out);
                free(chapter_utf8);
                // 章节之间写入换行（最后一章后也可以不加，看需求）
                if (i + 1 < chapter_num) fputs("\n\n", out);
            } else {
                fprintf(stderr, "[ERROR] UTF‑16LE conversion failed for chapter %zu\n", i+1);
                fputs("[conversion error]\n", out);
            }
        }

        fclose(out);
        printf("[INFO] Text saved to %s (chapters separated)\n", file_path);
    }
    return true;
}

int main(int argc, char **argv)
{
    const char *program = args_shift(&argc, &argv);

    if (argc <= 0) {
        fprintf(stderr, "[Usage] %s <umd file>\n", program);
        fprintf(stderr, "[ERROR] no umd file is provided\n");
        return 1;
    }

    // https://dl.wenku8.com/down.php?type=umd&id=1854&vsize=0&vid=1
    // https://dl.wenku8.com/down.php?type=umd&id=4109&vsize=0&vid=1
    const char *file_path = args_shift(&argc, &argv);

    printf("[INFO] Reading %s\n", file_path);
    size_t bytesRead = 0;
    const unsigned char *buffer = generate_buffer(file_path, &bytesRead);
    uint32_t rp = 0;
    printf("[INFO] Buffer generated\n");

    printf("[INFO] Reading file header\n");
    // Fixed head
    read_from_buffer(buffer, &rp, 4);
    // Invalid data
    read_from_buffer(buffer, &rp, 5);

    // Type ID
    uint32_t file_type = u32_from_buffer(buffer, &rp, 1);
    if (file_type == 1) {
        printf("[HEAD] File type: 1(txt)\n");
    }
    if (file_type == 2) {
        printf("[HEAD] File type: 2(img)\n");
        fprintf(stderr, "[ERROR] Unsupported type ID (2), only 1 has been supported");
        return 1;
    }else if (file_type != 1) {
        fprintf(stderr, "[ERROR] Unknown type ID (1: txt, 2: img)");
        return 1;
    }
    // Invalid data
    read_from_buffer(buffer, &rp, 2);

    printf("[INFO] Reading metadata\n");
    for (size_t i = 2; i <= 10; ++i) {
        // Separator (#)
        read_from_buffer(buffer, &rp, 1);

        // Meta type
        uint32_t meta_type = u32_from_buffer(buffer, &rp, 2);
        switch (meta_type) {
            case  2: printf("[META] Title: ");break;
            case  3: printf("[META] Author: ");break;
            case  4: printf("[META] Year: ");break;
            case  5: printf("[META] Month: ");break;
            case  6: printf("[META] Date: ");break;
            case  7: printf("[META] Type: ");break;
            case  8: printf("[META] Publish: ");break;
            case  9: printf("[META] Dealer: ");break;
            case 11: printf("[META] Total length: ");break;
            default: break;
        }

        // Invalid data
        read_from_buffer(buffer, &rp, 1);

        // Length of metadata
        uint32_t meta_len = u32_from_buffer(buffer, &rp, 1);

        if (meta_len < 5) {
            fprintf(stderr, "[ERROR] Invalid meta_len %u\n", meta_len);
            return 1;
        }

        if (rp + meta_len - 5 > bytesRead) {
            fprintf(stderr, "[ERROR] Metadata extends beyond buffer\n");
            return 1;
        }

        // Metadata
        if (meta_type == 11) {
            uint32_t content_len = u32_from_buffer(buffer, &rp, (meta_len - 5));
            printf("%u\n", content_len);
        }else {
            char *text = str_from_buffer(buffer, &rp, (meta_len - 5));
            if (text) {
                printf("%s\n", text);
                free(text);
            } else {
                printf("[bad encoding]\n");
            }
        }
    }

    printf("[INFO] Reading chapter contents\n");
    // Separator
    read_from_buffer(buffer, &rp, 1);
    // Sign
    read_from_buffer(buffer, &rp, 2);
    // Invalid data
    read_from_buffer(buffer, &rp, 11);
    // Nums
    uint32_t chapter_num = u32_from_buffer(buffer, &rp, 4);
    chapter_num = (chapter_num - 9) / 4;
    printf("[CHAP] there are %d chapters\n", chapter_num);

    Chapter *chapters = malloc(chapter_num*sizeof(Chapter));

    for (size_t i = 0; i < chapter_num; ++i) {
        chapters[i].offset = u32_from_buffer(buffer, &rp, 4);
        // rp -= 4;
        // printf("%02zu: %10u ->", i+1, chapters[i].offset);
        // print_from_buffer(buffer, &rp, 4);
    }

    read_from_buffer(buffer, &rp, 1);
    read_from_buffer(buffer, &rp, 2);
    read_from_buffer(buffer, &rp, 15);

    for (size_t i = 0; i < chapter_num; ++i) {
        printf("%03zu: ", i+1);
        chapters[i].title_len = u32_from_buffer(buffer, &rp, 1);
        chapters[i].title = rp;
        char *text = str_from_buffer(buffer, &rp, chapters[i].title_len);
        if (text) {
            printf("%s\n", text); free(text);
        } else {
            printf("[bad encoding]\n");
        }
    }

    // After reading the titles, we need to deal with the content of the book
    // A content block begins with the sign 0x24($), then comes 4 bytes of invalid data. 
    // Then comes 4 bytes of uint32_t, which represents the length of content(Zlib) after minus 9.
    //
    // The content of the text has been Zlib Compression, so it needs to be processed.
    //
    // After dealing with the compressed data, there are several situation according to the next byte:
    //     0x24: just deal with next block
    //     0x0A: jump 6 bytes and read next block
    //     0xF1: jump 18 bytes and read next block
    //     others: It usually means that the text has ended, and the reading of the file can be terminated. 
    //             Although there is a picture of the cover next, there is no need to read it.

    unsigned char *full_text = NULL;   // 解压后拼接的原始数据（UTF‑16LE）
    size_t full_text_len = 0;

    read_contents(buffer, &rp, bytesRead, &full_text, &full_text_len);
    save_content_as(buffer, "book.txt", chapters, chapter_num, full_text, full_text_len);

    // print_next_block(buffer, &rp, bytesRead);

    return 0;

}
