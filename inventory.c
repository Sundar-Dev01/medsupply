#include "medsupply.h"

void print_item_header(void)
{
    printf("\n%-5s %-24s %-14s %8s %10s %-11s %6s\n",
           "ID", "Name", "Category", "Qty", "Price", "Expiry", "MinLvl");
    printf("---------------------------------------------------------------------------------\n");
}

void print_item(const Item *it)
{
    printf("%-5d %-24.24s %-14.14s %8d %10.2f %-11s %6d\n",
           it->id, it->name, it->category, it->quantity, it->price, it->expiry, it->min_level);
}

int find_item_index(const Item *arr, int n, int id)
{
    int i;
    for (i = 0; i < n; i++) if (arr[i].id == id) return i;
    return -1;
}

void add_item(void)
{
    if (!require_role(ROLE_ADMIN)) return;
    int n, i, maxid = 0;
    Item *arr = fh_load_items(&n), *tmp, it;

    for (i = 0; i < n; i++) if (arr[i].id > maxid) maxid = arr[i].id;

    memset(&it, 0, sizeof it);
    it.id = maxid + 1;
    do { read_line("Item name       : ", it.name, sizeof it.name); } while (it.name[0] == '\0');
    read_line("Category        : ", it.category, sizeof it.category);
    it.quantity  = read_int("Quantity        : ", 0, 10000000);
    it.price     = read_float("Unit price      : ", 0.0f, 10000000.0f);
    for (;;) {
        read_line("Expiry (YYYY-MM-DD): ", it.expiry, sizeof it.expiry);
        if (valid_date(it.expiry)) break;
        printf("  Invalid date format.\n");
    }
    it.min_level = read_int("Low-stock level : ", 0, 10000000);

    tmp = realloc(arr, (size_t)(n + 1) * sizeof(Item));
    if (!tmp) { printf("Out of memory.\n"); free(arr); return; }
    arr = tmp;
    arr[n++] = it;
    if (fh_save_items(arr, n)) printf("Item added with ID %d.\n", it.id);
    free(arr);
}

void view_items(void)
{
    int n, i;
    Item *arr = fh_load_items(&n);
    if (n == 0) { printf("Inventory is empty.\n"); free(arr); return; }
    print_item_header();
    for (i = 0; i < n; i++) print_item(&arr[i]);
    printf("\nTotal items: %d\n", n);
    free(arr);
}

void search_item(void)
{
    int n, i, found = 0, choice;
    Item *arr = fh_load_items(&n);

    if (n == 0) { printf("Inventory is empty.\n"); free(arr); return; }

    printf("\n1. Search by ID\n2. Search by name / category\n");
    choice = read_int("Choice: ", 1, 2);

    if (choice == 1) {
        int id = read_int("Enter item ID: ", 1, 100000000);
        i = find_item_index(arr, n, id);
        if (i >= 0) { print_item_header(); print_item(&arr[i]); found = 1; }
    } else {
        char key[50];
        read_line("Enter keyword: ", key, sizeof key);
        for (i = 0; i < n; i++) {
            if (contains_ci(arr[i].name, key) || contains_ci(arr[i].category, key)) {
                if (!found) print_item_header();
                print_item(&arr[i]);
                found++;
            }
        }
    }
    if (!found) printf("No matching item found.\n");
    free(arr);
}

void delete_item(void)
{
    if (!require_role(ROLE_ADMIN)) return;
    int n, i, id, rn, pending = 0;
    Item *arr = fh_load_items(&n);
    Request *rq;

    if (n == 0) { printf("Inventory is empty.\n"); free(arr); return; }

    id = read_int("Enter ID of item to delete: ", 1, 100000000);
    i = find_item_index(arr, n, id);
    if (i < 0) { printf("Item not found.\n"); free(arr); return; }

    print_item_header(); print_item(&arr[i]);

    /* Do not orphan open requests */
    rq = fh_load_requests(&rn);
    for (int k = 0; k < rn; k++)
        if (rq[k].item_id == id && (rq[k].status == REQ_PENDING || rq[k].status == REQ_APPROVED))
            pending++;
    free(rq);
    if (pending) {
        printf("Cannot delete: %d open request(s) (pending/approved) reference this item.\n", pending);
        free(arr);
        return;
    }

    if (confirm("Delete this item permanently? (y/n): ")) {
        memmove(&arr[i], &arr[i + 1], (size_t)(n - i - 1) * sizeof(Item));
        if (fh_save_items(arr, n - 1)) printf("Item deleted.\n");
    } else printf("Cancelled.\n");
    free(arr);
}

void inventory_menu(int role)
{
    int c;
    if (role == ROLE_ADMIN) {
        do {
            printf("\n===== INVENTORY =====\n"
                   "1. Add Item\n2. View All Items\n3. Search\n4. Delete\n0. Back\n");
            c = read_int("Choice: ", 0, 4);
            switch (c) {
                case 1: add_item();    break;
                case 2: view_items();  break;
                case 3: search_item(); break;
                case 4: delete_item(); break;
            }
        } while (c != 0);
    } else {
        do {
            printf("\n===== ITEM LOOKUP =====\n"
                   "1. View All Items\n2. Search\n0. Back\n");
            c = read_int("Choice: ", 0, 2);
            switch (c) {
                case 1: view_items();  break;
                case 2: search_item(); break;
            }
        } while (c != 0);
    }
}
