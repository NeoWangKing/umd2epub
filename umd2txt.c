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

    for (size_t i = 0; i < bytesRead; ++i) {
        if (buffer[i] == 0x23 && (buffer[i+3] == 00 || buffer[i+3] == 01)) printf("\n");
        if (buffer[i] == 0x24) printf("\n");
        printf("%02x ", buffer[i]);
    }

    fclose(file);

    return 0;

}
