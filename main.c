/*
 * Medical Supply Management System  (with Admin / Staff login)
 *
 *   Login Screen  ->  Admin Menu  or  Staff Menu
 *
 *   ADMIN                                   STAFF
 *   |-- Inventory   Add, View, Search,      |-- Item Lookup  View, Search
 *   |               Delete                  |-- Requests     New, View Status
 *   |-- Stock       Update, Verify          |-- Distribution Issue, History
 *   |-- Requests    View, Approve, Reject   '-- Change My Password
 *   |-- Distribution History
 *   |-- User Management
 *   '-- Change My Password
 *           |
 *      File Handling
 *           |
 *    Data Storage Files (inventory / requests / history / users .dat)
 */
#include "medsupply.h"

static void admin_menu(void)
{
    int c;
    do {
        printf("\n=========================================\n"
               "   ADMIN MENU   (logged in as %s)\n"
               "=========================================\n"
               "1. Inventory\n2. Stock\n3. Requests\n4. Distribution\n"
               "5. User Management\n6. Change My Password\n0. Logout\n",
               g_current_user.username);
        c = read_int("Choice: ", 0, 6);
        switch (c) {
            case 1: inventory_menu(ROLE_ADMIN);    break;
            case 2: stock_menu();                  break;
            case 3: requests_menu(ROLE_ADMIN);     break;
            case 4: distribution_menu(ROLE_ADMIN); break;
            case 5: user_management_menu();        break;
            case 6: change_own_password();         break;
        }
    } while (c != 0);
}

static void staff_menu(void)
{
    int c;
    do {
        printf("\n=========================================\n"
               "   STAFF MENU   (logged in as %s)\n"
               "=========================================\n"
               "1. Items\n2. Requests\n3. Distribution\n4. Change My Password\n0. Logout\n",
               g_current_user.username);
        c = read_int("Choice: ", 0, 4);
        switch (c) {
            case 1: inventory_menu(ROLE_STAFF);    break;
            case 2: requests_menu(ROLE_STAFF);     break;
            case 3: distribution_menu(ROLE_STAFF); break;
            case 4: change_own_password();         break;
        }
    } while (c != 0);
}

int main(void)
{
    int c;
    fh_init_files();
    auth_init();

    for (;;) {
        printf("\n=========================================\n"
               "   MEDICAL SUPPLY MANAGEMENT SYSTEM\n"
               "=========================================\n"
               "1. Login\n0. Exit\n");
        c = read_int("Choice: ", 0, 1);
        if (c == 0) break;

        if (!auth_login()) {
            printf("Too many failed attempts. Exiting.\n");
            return 1;
        }
        if (g_current_user.role == ROLE_ADMIN) admin_menu();
        else                                   staff_menu();

        memset(&g_current_user, 0, sizeof g_current_user);
        printf("Logged out.\n");
    }

    printf("Goodbye.\n");
    return 0;
}
