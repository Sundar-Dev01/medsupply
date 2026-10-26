#include "medsupply.h"

void issue_supplies(void)
{
    if (!require_role(ROLE_STAFF)) return;
    int rn, in, i, k, id, hn;
    Request *reqs = fh_load_requests(&rn);
    Item *items;
    Distribution d, *h;
    int maxid = 0;

    list_requests(REQ_APPROVED);
    {   /* anything approved at all? */
        int any = 0;
        for (i = 0; i < rn; i++) if (reqs[i].status == REQ_APPROVED) any = 1;
        if (!any) { free(reqs); return; }
    }

    id = read_int("Request ID to issue (0 to cancel): ", 0, 100000000);
    if (id == 0) { free(reqs); return; }
    for (i = 0; i < rn && reqs[i].req_id != id; i++) ;
    if (i == rn || reqs[i].status != REQ_APPROVED) {
        printf("No approved request with that ID.\n"); free(reqs); return;
    }

    items = fh_load_items(&in);
    k = find_item_index(items, in, reqs[i].item_id);
    if (k < 0) { printf("Item no longer exists.\n"); free(items); free(reqs); return; }
    if (days_until(items[k].expiry) < 0) {
        printf("Cannot issue: '%s' is expired (%s).\n", items[k].name, items[k].expiry);
        free(items); free(reqs); return;
    }
    if (items[k].quantity < reqs[i].qty) {
        printf("Insufficient stock: have %d, need %d. Update stock first.\n", items[k].quantity, reqs[i].qty);
        free(items); free(reqs); return;
    }

    /* next distribution id */
    h = fh_load_history(&hn);
    for (int j = 0; j < hn; j++) if (h[j].dist_id > maxid) maxid = h[j].dist_id;
    free(h);

    memset(&d, 0, sizeof d);
    d.dist_id = maxid + 1;
    d.req_id  = reqs[i].req_id;
    d.item_id = items[k].id;
    d.qty     = reqs[i].qty;
    snprintf(d.item_name, sizeof d.item_name, "%s", items[k].name);
    snprintf(d.recipient, sizeof d.recipient, "%s", reqs[i].requester);
    get_today(d.date);
    snprintf(d.issued_by, sizeof d.issued_by, "%s", g_current_user.username);

    items[k].quantity -= reqs[i].qty;
    reqs[i].status = REQ_ISSUED;
    strcpy(reqs[i].remarks, "Issued");

    if (fh_save_items(items, in) && fh_save_requests(reqs, rn) && fh_append_history(&d))
        printf("Issued %d x %s to %s. Remaining stock: %d\n",
               d.qty, d.item_name, d.recipient, items[k].quantity);
    free(items); free(reqs);
}

void view_history(void)
{
    int n, i, total = 0;
    Distribution *h = fh_load_history(&n);

    if (n == 0) { printf("No distribution history yet.\n"); free(h); return; }

    printf("\n%-6s %-6s %-24s %-20s %6s %-11s %s\n", "DistID", "ReqID", "Item", "Recipient", "Qty", "Date", "IssuedBy");
    printf("-----------------------------------------------------------------------------\n");
    for (i = 0; i < n; i++) {
        printf("%-6d %-6d %-24.24s %-20.20s %6d %-11s %s\n",
               h[i].dist_id, h[i].req_id, h[i].item_name, h[i].recipient, h[i].qty, h[i].date, h[i].issued_by);
        total += h[i].qty;
    }
    printf("\n%d distributions, %d units issued in total.\n", n, total);
    free(h);
}

void distribution_menu(int role)
{
    int c;
    if (role == ROLE_ADMIN) {
        do {
            printf("\n===== DISTRIBUTION (REPORTS) =====\n1. History\n0. Back\n");
            c = read_int("Choice: ", 0, 1);
            if (c == 1) view_history();
        } while (c != 0);
    } else {
        do {
            printf("\n===== DISTRIBUTION =====\n1. Issue\n2. History\n0. Back\n");
            c = read_int("Choice: ", 0, 2);
            if (c == 1) issue_supplies();
            else if (c == 2) view_history();
        } while (c != 0);
    }
}
