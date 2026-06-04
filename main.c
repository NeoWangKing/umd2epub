#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <iconv.h>
#include <string.h>

/**
 * 将任意编码的字节串转换为 UTF-8 字符串。
 * @param data       原始字节
 * @param len        字节长度
 * @param from_enc   源编码名称（如 "UTF-16LE", "GBK"）
 * @return 成功返回 malloc 的 UTF-8 字符串，失败返回 NULL。调用者负责 free。
 */
char* convert_to_utf8(const unsigned char* data, size_t len, const char* from_enc)
{
    iconv_t cd = iconv_open("UTF-8", from_enc);
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

/**
 * 将 UTF-16LE 字节串（不含 BOM）转换为 UTF-8。
 * 自动跳过可选的 BOM (0xFF 0xFE)。
 */
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

uint32_t u32_from_buffer(unsigned char *buffer, uint32_t *start, uint32_t len)
{
    uint32_t data = 0;
    if (len > 4) len = 4;
    for (size_t i = 0; i < len; ++i) {
        data |= (uint32_t)buffer[*start] << (i * 8);
        *start += 1;
    }
    return data;
}

char * str_from_buffer(unsigned char *buffer, uint32_t *start, uint32_t len)
{
    const unsigned char *data = &buffer[*start];
    char *str = utf16le_to_utf8(data, len);
    *start += len;
    return str;
}

void print_from_buffer(unsigned char *buffer, uint32_t *start, uint32_t len)
{
    for (size_t i = 0; i < len; ++i) {
        printf("%02x ", buffer[*start]);
        *start += 1;
    }
    printf("\n");
}

void read_threw_buffer(unsigned char *buffer, uint32_t *start, uint32_t len)
{
    for (size_t i = 0; i < len; ++i) {
        *start += 1;
    }
}

int main(int argc, char **argv)
{
    char file_path[256] = "book.umd";
    printf("[INFO] Reading %s\n", file_path);

    FILE *file = NULL;

    file = fopen(file_path, "rb");
    if (file == NULL) {
        fprintf(stderr, "[ERROR] cannot open file %s", file_path);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 动态分配缓冲区
    unsigned char *buffer = malloc(file_size);
    if (!buffer) {
        fprintf(stderr, "[ERROR] error when malloc buffer %s", file_path);
        fclose(file);
        return 1;
    }
    size_t bytesRead = fread(buffer, 1, file_size, file);
    if (bytesRead == 0) {
        fprintf(stderr, "[ERROR] cannot read file %s", file_path);
        fclose(file);
        return 1;
    }

    uint32_t read_pointer = 0;

    printf("\n");
    printf("[INFO] Reading file header\n");
    /*
     * Structure of file header:
     *     (4 bytes) fixed file head (0xde9a9b89)
     *     (5 bytes) invalid data
     *     (1 byte ) type of file (1: txt, 2: img)
     *     (2 bytes) invalid data
     */
    // Fixed head
    // printf("0x%04x\n", u32_from_buffer(buffer, &read_pointer, 4));
    read_threw_buffer(buffer, &read_pointer, 4);

    // Invalid data
    read_threw_buffer(buffer, &read_pointer, 5);

    // Type ID
    uint32_t file_type = u32_from_buffer(buffer, &read_pointer, 1);
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
    read_threw_buffer(buffer, &read_pointer, 2);

    printf("[INFO] Reading metadata\n");
    /*
     * Structure of metadata:
     *     (1 byte ) '#'
     *     (2 bytes) type id of metadata
     *         id type 类型
     *         ---------------------------------
     *         2  text 标题
     *         3  text 作者
     *         4  text 出版年
     *         5  text 出版月
     *         6  text 出版日
     *         7  text 类型
     *         8  text 出版社
     *         9  text 经销商
     *         11 int  未经压缩的内容总长度
     *     (1 byte ) invalid data
     *     (1 byte ) length of metadata
     *     (n bytes) meta data (length - 5)
     */
    for (size_t i = 2; i <= 10; ++i) {
        // Separator (#)
        read_threw_buffer(buffer, &read_pointer, 1);

        // Meta type
        uint32_t meta_type = u32_from_buffer(buffer, &read_pointer, 2);
        switch (meta_type) {
            case 2: printf("[META] Title: ");break;
            case 3: printf("[META] Author: ");break;
            case 4: printf("[META] Year: ");break;
            case 5: printf("[META] Month: ");break;
            case 6: printf("[META] Date: ");break;
            case 7: printf("[META] Type: ");break;
            case 8: printf("[META] Publish: ");break;
            case 9: printf("[META] Dealer: ");break;
            case 11: printf("[META] Total length: ");break;
            default: break;
        }

        // Invalid data
        read_threw_buffer(buffer, &read_pointer, 1);

        // Length of metadata
        uint32_t meta_len = u32_from_buffer(buffer, &read_pointer, 1);

        if (meta_len < 5) {
            fprintf(stderr, "[ERROR] Invalid meta_len %u\n", meta_len);
            fclose(file);
            return 1;
        }

        if (read_pointer + meta_len - 5 > bytesRead) {
            fprintf(stderr, "[ERROR] Metadata extends beyond buffer\n");
            fclose(file);
            return 1;
        }

        // Metadata
        if (meta_type == 11) {
            uint32_t content_len = u32_from_buffer(buffer, &read_pointer, (meta_len - 5));
            printf("%u\n", content_len);
        }else {
            char *text = str_from_buffer(buffer, &read_pointer, (meta_len - 5));
            if (text) {
                printf("%s\n", text);
                free(text);
            } else {
                printf("[bad encoding]\n");
            }
        }
    }

    printf("[INFO] Reading chapter contents\n");
    /*
     * Structure of chapter contents:
     *     (1  byte ) '#': separator
     *     (2  bytes) '0x0083': sign of chapter
     *     (11 bytes) invalid data
     *     (4  bytes) uint32_t: nums of chapters ((num - 9)/4)
     *     (4  bytes) uint32_t: Represents the offset at the beginning of the chapter
     *                          after the content of the article is decompressed.
     */

    // Separator
    read_threw_buffer(buffer, &read_pointer, 1);

    // Sign
    read_threw_buffer(buffer, &read_pointer, 2);

    // Invalid data
    read_threw_buffer(buffer, &read_pointer, 11);

    // Nums
    uint32_t chapter_num = u32_from_buffer(buffer, &read_pointer, 4);
    chapter_num = (chapter_num - 9) / 4;
    printf("[CHAP] there are %u chapters\n", chapter_num);

    uint32_t *chapter_offset = malloc(chapter_num*sizeof(uint32_t));

    for (size_t i = 0; i < chapter_num; ++i) {
        chapter_offset[i] = u32_from_buffer(buffer, &read_pointer, 4);
        printf("%03zu: %u\n", i+1, chapter_offset[i]);
    }

    // print_from_buffer(buffer, &read_pointer, 1);
    // print_from_buffer(buffer, &read_pointer, 2);
    // read_threw_buffer(buffer, &read_pointer, 15);
    //
    // for (size_t i = 0; i < chapter_num; ++i) {
    //     printf("%03zu: ", i);
    //     uint32_t chap_title_len = u32_from_buffer(buffer, &read_pointer, 1);
    //     char *text = str_from_buffer(buffer, &read_pointer, chap_title_len);
    //     if (text) {
    //         printf("%s\n", text);
    //         free(text);
    //     } else {
    //         printf("[bad encoding]\n");
    //     }
    // }

    for (size_t i = read_pointer; i < read_pointer + 100; ++i) {
        printf("%02x ", buffer[i]);
    }
    printf("\n");
    printf("[INFO] Closing file\n");
    fclose(file);

    return 0;

}
