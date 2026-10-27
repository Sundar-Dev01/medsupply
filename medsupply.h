#ifndef MEDSUPPLY_H
#define MEDSUPPLY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ---------- Data storage files ---------- */
#define ITEMS_FILE    "inventory.dat"
#define REQUESTS_FILE "requests.dat"
#define HISTORY_FILE  "history.dat"
#define USERS_FILE    "users.dat"

/* ---------- User roles / login settings ---------- */
#define ROLE_ADMIN 1
#define ROLE_STAFF 2
#define MAX_LOGIN_ATTEMPTS 3
#define MIN_PASSWORD_LEN   6
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASS "admin123"

/* ---------- Request status codes ---------- */
#define REQ_PENDING  0
#define REQ_APPROVED 1
#define REQ_REJECTED 2
#define REQ_ISSUED   3

/* ---------- Record structures ---------- */
typedef struct {
    int   id;
    char  name[50];
    char  category[30];
    int   quantity;
    float price;
    char  expiry[11];      /* YYYY-MM-DD */
    int   min_level;       /* reorder / low-stock threshold */
} Item;

typedef struct {
    int  req_id;
    int  item_id;
    char requester[40];
    int  qty;
    int  status;
    char date[20];
    char remarks[60];
    char created_by[24];   /* username that created the request */
} Request;

typedef struct {
    int  dist_id;
    int  req_id;
    int  item_id;
    char item_name[50];
    char recipient[40];
    int  qty;
    char date[20];
    char issued_by[24];    /* username that issued the supplies */
} Distribution;

typedef struct {
    char username[24];
    char salt[17];
    char hash[17];         /* salted, iterated hash - never the plain password */
    int  role;             /* ROLE_ADMIN or ROLE_STAFF */
} User;

extern User g_current_user;    /* the logged-in user (set by auth_login) */

/* ---------- utils.c ---------- */
void  read_line(const char *prompt, char *buf, int size);
int   read_int(const char *prompt, int min, int max);
float read_float(const char *prompt, float min, float max);
int   confirm(const char *prompt);
void  get_today(char *out);                 /* YYYY-MM-DD */
int   get_timestamp(char *out, size_t out_size); /* YYYY-MM-DD HH:MM:SS */
int   log_event(const char *event, const char *actor, const char *details);
int valid_date(const char *date);
int   days_until(const char *date);         /* days from today to date */
int   contains_ci(const char *text, const char *key);

/* ---------- filehandler.c (File Handling layer) ---------- */
Item         *fh_load_items(int *count);
int           fh_save_items(const Item *array, int count);

Request      *fh_load_requests(int *count);
int           fh_save_requests(const Request *array, int count);

Distribution *fh_load_history(int *count);
int           fh_append_history(const Distribution *record);

User         *fh_load_users(int *count);
int           fh_save_users(const User *array, int count);

void          fh_init_files(void);

/* ---------- auth.c (login & user management) ---------- */
void        auth_init(void);                 /* creates default admin if no users exist */
int         auth_login(void);                /* 1 = logged in, 0 = too many failed attempts */
int         require_role(int role);          /* 1 = allowed, 0 = prints "Access denied" */
const char *role_name(int role);
void        read_password(const char *prompt, char *buf, int size);
void        change_own_password(void);
void        user_management_menu(void);

/* ---------- inventory.c ---------- */
void print_item_header(void);
void print_item(const Item *it);
int  find_item_index(const Item *arr, int n, int id);
void inventory_menu(int role);
void add_item(void);
void view_items(void);
void search_item(void);
void delete_item(void);

/* ---------- stock.c ---------- */
void stock_menu(void);
void update_stock(void);
void verify_stock(void);

/* ---------- requests.c ---------- */
void requests_menu(int role);
void create_request(void);
void list_requests(int status_filter);
void approve_request(void);
void reject_request(void);
const char *status_name(int s);
int  reserved_qty(const Request *r, int n, int item_id);

/* ---------- distribution.c ---------- */
void distribution_menu(int role);
void issue_supplies(void);
void view_history(void);

#endif
