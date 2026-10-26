#include "medsupply.h"

void read_line(const char *prompt, char *buf, int size)
{
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    if (!fgets(buf, size, stdin)) {          /* EOF -> leave cleanly */
        printf("\n");
        exit(0);
    }
    if (strchr(buf, '\n')) buf[strcspn(buf, "\n")] = '\0';
    else { int c; while ((c = getchar()) != '\n' && c != EOF) ; } /* flush overflow */
}

int read_int(const char *prompt, int min, int max)
{
    char buf[64], *end;
    long v;
    for (;;) {
        read_line(prompt, buf, sizeof buf);
        v = strtol(buf, &end, 10);
        if (end != buf && *end == '\0' && v >= min && v <= max) return (int)v;
        printf("  Invalid input. Enter a number between %d and %d.\n", min, max);
    }
}

float read_float(const char *prompt, float min, float max)
{
    char buf[64], *end;
    float v;
    for (;;) {
        read_line(prompt, buf, sizeof buf);
        v = strtof(buf, &end);
        if (end != buf && *end == '\0' && v >= min && v <= max) return v;
        printf("  Invalid input. Enter a value between %.2f and %.2f.\n", min, max);
    }
}

int confirm(const char *prompt)
{
    char buf[16];
    read_line(prompt, buf, sizeof buf);
    return (buf[0] == 'y' || buf[0] == 'Y');
}

void get_today(char *out)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(out, 11, "%Y-%m-%d", tm);
}

int valid_date(const char *s)
{
    int y, m, d, i;
    if (strlen(s) != 10) return 0;
    for (i = 0; i < 10; i++) {
        if (i == 4 || i == 7) { if (s[i] != '-') return 0; }
        else if (!isdigit((unsigned char)s[i])) return 0;
    }
    sscanf(s, "%d-%d-%d", &y, &m, &d);
    return (y >= 2000 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31);
}

static time_t date_to_time(const char *s)
{
    struct tm tm;
    int y, m, d;
    memset(&tm, 0, sizeof tm);
    sscanf(s, "%d-%d-%d", &y, &m, &d);
    tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
    tm.tm_hour = 12; tm.tm_isdst = -1;
    return mktime(&tm);
}

int days_until(const char *date)
{
    char today[11];
    get_today(today);
    return (int)(difftime(date_to_time(date), date_to_time(today)) / 86400.0 + (difftime(date_to_time(date), date_to_time(today)) >= 0 ? 0.5 : -0.5));
}

int contains_ci(const char *text, const char *key)
{
    size_t i, j, tl = strlen(text), kl = strlen(key);
    if (kl == 0) return 1;
    for (i = 0; i + kl <= tl; i++) {
        for (j = 0; j < kl; j++)
            if (tolower((unsigned char)text[i + j]) != tolower((unsigned char)key[j])) break;
        if (j == kl) return 1;
    }
    return 0;
}
