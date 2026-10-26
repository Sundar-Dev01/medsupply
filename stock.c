#include "medsupply.h"

void update_stock(void)
{
    if (!require_role(ROLE_ADMIN)) return;
    int n, i, id, mode, qty;
    Item *arr = fh_load_items(&n);

    if (n == 0) { printf("Inventory is empty. Add items first.\n"); free(arr); return; }

    id = read_int("Enter item ID: ", 1, 100000000);
    i = find_item_index(arr, n, id);
    if (i < 0) { printf("Item not found.\n"); free(arr); return; }

    print_item_header(); print_item(&arr[i]);
    printf("\n1. Add stock (received)\n2. Remove stock (damaged/expired/adjust)\n3. Set exact quantity\n");
    mode = read_int("Choice: ", 1, 3);

    if (mode == 1) {
        qty = read_int("Quantity to add: ", 1, 10000000);
        if ((long)arr[i].quantity + qty > 100000000L) { printf("Quantity too large.\n"); free(arr); return; }
        arr[i].quantity += qty;
    } else if (mode == 2) {
        qty = read_int("Quantity to remove: ", 1, 10000000);
        if (qty > arr[i].quantity) { printf("Cannot remove more than current stock (%d).\n", arr[i].quantity); free(arr); return; }
        arr[i].quantity -= qty;
    } else {
        arr[i].quantity = read_int("New quantity: ", 0, 100000000);
    }

    if (fh_save_items(arr, n)) printf("Stock updated. New quantity of '%s': %d\n", arr[i].name, arr[i].quantity);
    free(arr);
}

void verify_stock(void)
{
    if (!require_role(ROLE_ADMIN)) return;
    int n, i, low = 0, expired = 0, soon = 0, changed = 0;
    Item *arr = fh_load_items(&n);

    if (n == 0) { printf("Inventory is empty.\n"); free(arr); return; }

    printf("\n===== STOCK VERIFICATION =====\n");
    printf("%-5s %-24s %8s %-11s %s\n", "ID", "Name", "Qty", "Expiry", "Status");
    printf("--------------------------------------------------------------------\n");
    for (i = 0; i < n; i++) {
        int d = days_until(arr[i].expiry);
        char status[80] = "";
        if (d < 0)               { strcat(status, "EXPIRED "); expired++; }
        else if (d <= 30)        { strcat(status, "EXPIRES-SOON "); soon++; }
        if (arr[i].quantity == 0){ strcat(status, "OUT-OF-STOCK "); low++; }
        else if (arr[i].quantity <= arr[i].min_level) { strcat(status, "LOW-STOCK "); low++; }
        if (status[0] == '\0') strcpy(status, "OK");
        printf("%-5d %-24.24s %8d %-11s %s\n", arr[i].id, arr[i].name, arr[i].quantity, arr[i].expiry, status);
    }
    printf("\nSummary: %d low/out of stock, %d expired, %d expiring within 30 days.\n", low, expired, soon);

    /* Physical count reconciliation */
    if (confirm("\nReconcile with a physical count? (y/n): ")) {
        for (;;) {
            int id = read_int("Item ID to reconcile (0 to finish): ", 0, 100000000);
            int counted;
            if (id == 0) break;
            i = find_item_index(arr, n, id);
            if (i < 0) { printf("  Item not found.\n"); continue; }
            counted = read_int("  Physical count: ", 0, 100000000);
            if (counted == arr[i].quantity) {
                printf("  Verified: record matches physical count (%d).\n", counted);
            } else {
                printf("  MISMATCH: record=%d, counted=%d (difference %+d)\n",
                       arr[i].quantity, counted, counted - arr[i].quantity);
                if (confirm("  Correct the record to the physical count? (y/n): ")) {
                    arr[i].quantity = counted; changed = 1;
                    printf("  Record corrected.\n");
                }
            }
        }
        if (changed && fh_save_items(arr, n)) printf("Corrections saved.\n");
    }
    free(arr);
}

void stock_menu(void)
{
    int c;
    do {
        printf("\n===== STOCK =====\n1. Update\n2. Verify\n0. Back\n");
        c = read_int("Choice: ", 0, 2);
        if (c == 1) update_stock();
        else if (c == 2) verify_stock();
    } while (c != 0);
}
