/* File Handling layer: all reads/writes of the Data Storage Files live here. */
#include "medsupply.h"

/* Generic loader: returns malloc'd array (or NULL if empty) and sets *count */
static void *load_all(const char *file, size_t size, int *count)
{
    FILE *fp = fopen(file, "rb");
    long bytes;
    void *buf;

    *count = 0;
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    bytes = ftell(fp);
    rewind(fp);
    if (bytes < (long)size) { fclose(fp); return NULL; }

    *count = (int)(bytes / (long)size);
    buf = malloc((size_t)*count * size);
    if (!buf) { fclose(fp); *count = 0; return NULL; }
    if (fread(buf, size, (size_t)*count, fp) != (size_t)*count) {
        printf("Warning: could not fully read %s\n", file);
        free(buf); fclose(fp); *count = 0; return NULL;
    }
    fclose(fp);
    return buf;
}

static int save_all(const char *file, const void *arr, size_t size, int count)
{
    FILE *fp = fopen(file, "wb");
    if (!fp) { printf("Error: cannot write to %s\n", file); return 0; }
    if (count > 0 && fwrite(arr, size, (size_t)count, fp) != (size_t)count) {
        printf("Error: write failed for %s\n", file);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

Item *fh_load_items(int *count)            { return load_all(ITEMS_FILE, sizeof(Item), count); }
int   fh_save_items(const Item *a, int n)  { return save_all(ITEMS_FILE, a, sizeof(Item), n); }

Request *fh_load_requests(int *count)               { return load_all(REQUESTS_FILE, sizeof(Request), count); }
int      fh_save_requests(const Request *a, int n)  { return save_all(REQUESTS_FILE, a, sizeof(Request), n); }

User *fh_load_users(int *count)            { return load_all(USERS_FILE, sizeof(User), count); }
int   fh_save_users(const User *a, int n)  { return save_all(USERS_FILE, a, sizeof(User), n); }

Distribution *fh_load_history(int *count) { return load_all(HISTORY_FILE, sizeof(Distribution), count); }

int fh_append_history(const Distribution *rec)
{
    FILE *fp = fopen(HISTORY_FILE, "ab");
    if (!fp) { printf("Error: cannot open %s\n", HISTORY_FILE); return 0; }
    fwrite(rec, sizeof *rec, 1, fp);
    fclose(fp);
    return 1;
}

/* Create the data files if they do not exist yet */
void fh_init_files(void)
{
    const char *files[] = { ITEMS_FILE, REQUESTS_FILE, HISTORY_FILE, USERS_FILE };
    int i;
    for (i = 0; i < 4; i++) {
        FILE *fp = fopen(files[i], "ab");
        if (fp) fclose(fp);
        else printf("Warning: cannot create %s\n", files[i]);
    }
}
