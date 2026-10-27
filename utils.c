#include "medsupply.h"
#include <errno.h>
#include <math.h>

void read_line(const char *prompt, char *buf, int size)
{
    size_t length;
    int ch;
    if ((buf == NULL) || (size <= 0)) {
        return;
    }
    if (prompt != NULL) {
        (void)printf("%s", prompt);
        (void)fflush(stdout);
    }
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        (void)printf("\\n");
        exit(EXIT_SUCCESS);
    }
    length = strlen(buf);
    if ((length > 0U) && (buf[length - 1U] == '\n')) {
        buf[length - 1U] = '\0';
    } else {
        do {
            ch = getchar();
        } while ((ch != '\n') && (ch != EOF));
    }
}

int read_int(const char *prompt, int min, int max)
{
    char buf[64];
    char *end;
    long value;
    for (;;) {
        read_line(prompt, buf, (int)sizeof buf);
        errno = 0;
        value = strtol(buf, &end, 10);
        if ((end != buf) && (*end == '\0') && (errno == 0) &&
            (value >= (long)min) && (value <= (long)max)) {
            return (int)value;
        }
        (void)printf("  Invalid input. Enter a number between %d and %d.\n", min, max);
    }
}

float read_float(const char *prompt, float min, float max)
{
    char buf[64];
    char *end;
    float value;
    for (;;) {
        read_line(prompt, buf, (int)sizeof buf);
        errno = 0;
        value = strtof(buf, &end);
        if ((end != buf) && (*end == '\0') && (errno == 0) &&
            isfinite(value) && (value >= min) && (value <= max)) {
            return value;
        }
        (void)printf("  Invalid input. Enter a value between %.2f and %.2f.\n", min, max);
    }
}

int confirm(const char *prompt)
{
    char buf[16];
    read_line(prompt, buf, (int)sizeof buf);
    return ((buf[0] == 'y') || (buf[0] == 'Y'));
}

int get_timestamp(char *out, size_t out_size)
{
    time_t now;
    const struct tm *local;
    if ((out == NULL) || (out_size < 20U)) {
        return 0;
    }
    now = time(NULL);
    local = localtime(&now);
    if (local == NULL) {
        out[0] = '\0';
        return 0;
    }
    return (strftime(out, out_size, "%Y-%m-%d %H:%M:%S", local) > 0U) ? 1 : 0;
}

void get_today(char *out)
{
    time_t now;
    const struct tm *local;
    if (out == NULL) {
        return;
    }
    now = time(NULL);
    local = localtime(&now);
    if (local == NULL) {
        out[0] = '\0';
        return;
    }
    if (strftime(out, 11U, "%Y-%m-%d", local) == 0U) {
        out[0] = '\0';
    }
}

int log_event(const char *event, const char *actor, const char *details)
{
    FILE *fp;
    char timestamp[20];
    int result;
    if ((event == NULL) || (actor == NULL) || (details == NULL) ||
        (strchr(event, '\n') != NULL) || (strchr(actor, '\n') != NULL) ||
        (strchr(details, '\n') != NULL) || (strchr(event, '\r') != NULL) ||
        (strchr(actor, '\r') != NULL) || (strchr(details, '\r') != NULL)) {
        return 0;
    }
    if (get_timestamp(timestamp, sizeof timestamp) == 0) {
        return 0;
    }
    fp = fopen("audit.log", "a");
    if (fp == NULL) {
        return 0;
    }
    result = fprintf(fp, "%s | actor=%s | event=%s | details=%s\n",
                     timestamp, actor, event, details);
    {
        int flush_result = fflush(fp);
        int close_result = fclose(fp);
        if ((result < 0) || (flush_result != 0) || (close_result != 0)) {
            return 0;
        }
    }
    return 1;
}

int valid_date(const char *date)
{
    int year;
    int month;
    int day;
    int parsed;
    int leap;
    int max_day;
    size_t i;
    if ((date == NULL) || (strlen(date) != 10U)) {
        return 0;
    }
    for (i = 0U; i < 10U; i++) {
        if ((i == 4U) || (i == 7U)) {
            if (date[i] != '-') return 0;
        } else if (!isdigit((unsigned char)date[i])) {
            return 0;
        }
    }
    parsed = sscanf(date, "%4d-%2d-%2d", &year, &month, &day);
    if (parsed != 3) return 0;
    if ((year < 2000) || (year > 2100) || (month < 1) || (month > 12)) return 0;
    leap = (((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0))) ? 1 : 0;
    switch (month) {
        case 4: case 6: case 9: case 11: max_day = 30; break;
        case 2: max_day = (leap != 0) ? 29 : 28; break;
        default: max_day = 31; break;
    }
    return ((day >= 1) && (day <= max_day));
}

static time_t date_to_time(const char *date)
{
    struct tm value;
    int year;
    int month;
    int day;
    if (!valid_date(date) || (sscanf(date, "%4d-%2d-%2d", &year, &month, &day) != 3)) {
        return (time_t)-1;
    }
    memset(&value, 0, sizeof value);
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = 12;
    value.tm_isdst = -1;
    return mktime(&value);
}

int days_until(const char *date)
{
    char today[11];
    time_t target;
    time_t current;
    double difference;
    if (!valid_date(date)) return -1000000;
    get_today(today);
    target = date_to_time(date);
    current = date_to_time(today);
    if ((target == (time_t)-1) || (current == (time_t)-1)) return -1000000;
    difference = difftime(target, current) / 86400.0;
    return (int)((difference >= 0.0) ? (difference + 0.5) : (difference - 0.5));
}

int contains_ci(const char *text, const char *key)
{
    size_t i;
    size_t j;
    size_t text_len;
    size_t key_len;
    if ((text == NULL) || (key == NULL)) return 0;
    text_len = strlen(text);
    key_len = strlen(key);
    if (key_len == 0U) return 1;
    if (key_len > text_len) return 0;
    for (i = 0U; i <= text_len - key_len; i++) {
        for (j = 0U; j < key_len; j++) {
            if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)key[j])) break;
        }
        if (j == key_len) return 1;
    }
    return 0;
}
