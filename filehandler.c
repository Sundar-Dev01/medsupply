/* File Handling layer: all reads/writes of the Data Storage Files live here. */
#include "medsupply.h"
#include <limits.h>
#include <stdint.h>

static void *load_all(const char *file, size_t record_size, int *count)
{
    FILE *fp;
    long bytes;
    size_t records;
    void *buffer;
    if ((file == NULL) || (count == NULL) || (record_size == 0U)) return NULL;
    *count = 0;
    fp = fopen(file, "rb");
    if (fp == NULL) return NULL;
    if ((fseek(fp, 0L, SEEK_END) != 0) || ((bytes = ftell(fp)) < 0L) ||
        (fseek(fp, 0L, SEEK_SET) != 0)) {
        (void)fclose(fp);
        return NULL;
    }
    if (((unsigned long)bytes % record_size) != 0UL) {
        (void)printf("Warning: %s has a truncated/corrupt record.\n", file);
        (void)fclose(fp);
        return NULL;
    }
    records = (size_t)((unsigned long)bytes / record_size);
    if (records == 0U) {
        (void)fclose(fp);
        return NULL;
    }
    if ((records > (size_t)INT_MAX) || (records > (SIZE_MAX / record_size))) {
        (void)printf("Warning: %s is too large to load.\n", file);
        (void)fclose(fp);
        return NULL;
    }
    buffer = malloc(records * record_size);
    if (buffer == NULL) {
        (void)printf("Warning: out of memory reading %s.\n", file);
        (void)fclose(fp);
        return NULL;
    }
    if (fread(buffer, record_size, records, fp) != records) {
        (void)printf("Warning: could not fully read %s.\n", file);
        free(buffer);
        (void)fclose(fp);
        return NULL;
    }
    if (fclose(fp) != 0) {
        free(buffer);
        return NULL;
    }
    *count = (int)records;
    return buffer;
}

static int save_all(const char *file, const void *array, size_t record_size, int count)
{
    FILE *fp;
    size_t written;
    int close_result;
    if ((file == NULL) || (count < 0) || (record_size == 0U) ||
        ((count > 0) && (array == NULL))) return 0;
    fp = fopen(file, "wb");
    if (fp == NULL) {
        (void)printf("Error: cannot write to %s.\n", file);
        return 0;
    }
    written = (count > 0) ? fwrite(array, record_size, (size_t)count, fp) : 0U;
    if ((count > 0) && (written != (size_t)count)) {
        (void)printf("Error: write failed for %s.\n", file);
        (void)fclose(fp);
        return 0;
    }
    if (fflush(fp) != 0) {
        (void)fclose(fp);
        return 0;
    }
    close_result = fclose(fp);
    return (close_result == 0) ? 1 : 0;
}

Item *fh_load_items(int *count) { return (Item *)load_all(ITEMS_FILE, sizeof(Item), count); }
int fh_save_items(const Item *array, int count) { return save_all(ITEMS_FILE, array, sizeof(Item), count); }
Request *fh_load_requests(int *count) { return (Request *)load_all(REQUESTS_FILE, sizeof(Request), count); }
int fh_save_requests(const Request *array, int count) { return save_all(REQUESTS_FILE, array, sizeof(Request), count); }
User *fh_load_users(int *count) { return (User *)load_all(USERS_FILE, sizeof(User), count); }
int fh_save_users(const User *array, int count) { return save_all(USERS_FILE, array, sizeof(User), count); }
Distribution *fh_load_history(int *count) { return (Distribution *)load_all(HISTORY_FILE, sizeof(Distribution), count); }

int fh_append_history(const Distribution *record)
{
    FILE *fp;
    int result;
    if (record == NULL) return 0;
    fp = fopen(HISTORY_FILE, "ab");
    if (fp == NULL) {
        (void)printf("Error: cannot open %s.\n", HISTORY_FILE);
        return 0;
    }
    result = (fwrite(record, sizeof *record, 1U, fp) == 1U) ? 1 : 0;
    if ((fflush(fp) != 0) || (fclose(fp) != 0)) result = 0;
    return result;
}

void fh_init_files(void)
{
    const char *files[] = { ITEMS_FILE, REQUESTS_FILE, HISTORY_FILE, USERS_FILE };
    size_t i;
    for (i = 0U; i < (sizeof files / sizeof files[0]); i++) {
        FILE *fp = fopen(files[i], "ab");
        if (fp != NULL) {
            if (fclose(fp) != 0) (void)printf("Warning: cannot close %s.\n", files[i]);
        } else {
            (void)printf("Warning: cannot create %s.\n", files[i]);
        }
    }
}
