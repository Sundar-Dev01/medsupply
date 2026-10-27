#include "medsupply.h"

const char *status_name(int s)
{
    switch (s) {
        case REQ_PENDING:  return "PENDING";
        case REQ_APPROVED: return "APPROVED";
        case REQ_REJECTED: return "REJECTED";
        case REQ_ISSUED:   return "ISSUED";
    }
    return "UNKNOWN";
}

/* Quantity already promised to other approved (not yet issued) requests */
int reserved_qty(const Request *r, int n, int item_id)
{
    int i, total = 0;
    for (i = 0; i < n; i++)
        if (r[i].item_id == item_id && r[i].status == REQ_APPROVED) total += r[i].qty;
    return total;
}

void create_request(void)
{
    if (!require_role(ROLE_STAFF)) return;
    int in, rn, id, qty, i, maxid = 0;
    Item *items = fh_load_items(&in);
    Request *reqs, *tmp, r;

    if (in == 0) { printf("Inventory is empty.\n"); free(items); return; }

    id = read_int("Item ID requested: ", 1, 100000000);
    i = find_item_index(items, in, id);
    if (i < 0) { printf("Item not found.\n"); free(items); return; }
    printf("Item: %s (in stock: %d)\n", items[i].name, items[i].quantity);
    free(items);

    memset(&r, 0, sizeof r);
    do { read_line("Requester / department: ", r.requester, sizeof r.requester); } while (r.requester[0] == '\0');
    qty = read_int("Quantity needed: ", 1, 10000000);

    reqs = fh_load_requests(&rn);
    for (i = 0; i < rn; i++) if (reqs[i].req_id > maxid) maxid = reqs[i].req_id;

    r.req_id  = maxid + 1;
    r.item_id = id;
    r.qty     = qty;
    r.status  = REQ_PENDING;
    if (get_timestamp(r.date, sizeof r.date) == 0) {
    printf("Error: could not generate request timestamp.\n");
    free(reqs);
    return;
}
    snprintf(r.created_by, sizeof r.created_by, "%s", g_current_user.username);

    tmp = realloc(reqs, (size_t)(rn + 1) * sizeof(Request));
    if (!tmp) { printf("Out of memory.\n"); free(reqs); return; }
    reqs = tmp;
    reqs[rn++] = r;
    if (fh_save_requests(reqs, rn)) printf("Request #%d created (PENDING).\n", r.req_id);
    free(reqs);
}

/* status_filter: -1 = all */
void list_requests(int status_filter)
{
    int rn, in, i, shown = 0;
    Request *reqs = fh_load_requests(&rn);
    Item *items = fh_load_items(&in);

    for (i = 0; i < rn; i++) {
        int k;
        if (status_filter >= 0 && reqs[i].status != status_filter) continue;
        if (!shown)
        printf("\n%-5s %-22s %-20s %6s %-10s %-19s %-12s %s\n",
       "ReqID", "Item", "Requester", "Qty", "Status",
       "Timestamp", "By", "Remarks");


        k = find_item_index(items, in, reqs[i].item_id);
        printf("%-5d %-22.22s %-20.20s %6d %-10s %-19s %-12.12s %s\n",
        reqs[i].req_id,
        k >= 0 ? items[k].name : "(deleted item)",
        reqs[i].requester,
        reqs[i].qty,
        status_name(reqs[i].status),
        reqs[i].date,
        reqs[i].created_by,
        reqs[i].remarks);

        shown++;
    }
    if (!shown) printf("No requests to display.\n");
    free(reqs); free(items);
}

void approve_request(void)
{
    if (!require_role(ROLE_ADMIN)) return;
    int rn, in, i, k, id, available;
    Request *reqs = fh_load_requests(&rn);
    Item *items;

    list_requests(REQ_PENDING);
    if (rn == 0) { free(reqs); return; }

    id = read_int("Request ID to approve (0 to cancel): ", 0, 100000000);
    if (id == 0) { free(reqs); return; }
    for (i = 0; i < rn && reqs[i].req_id != id; i++) ;
    if (i == rn || reqs[i].status != REQ_PENDING) {
        printf("No pending request with that ID.\n"); free(reqs); return;
    }

    items = fh_load_items(&in);
    k = find_item_index(items, in, reqs[i].item_id);
    if (k < 0) { printf("Item no longer exists. Reject this request instead.\n"); free(items); free(reqs); return; }

    available = items[k].quantity - reserved_qty(reqs, rn, items[k].id);
    if (days_until(items[k].expiry) < 0) {
        printf("Cannot approve: '%s' is expired (%s).\n", items[k].name, items[k].expiry);
    } else if (reqs[i].qty > available) {
        printf("Cannot approve: requested %d but only %d available (stock %d minus reserved).\n",
               reqs[i].qty, available, items[k].quantity);
    } else {
        reqs[i].status = REQ_APPROVED;
        strcpy(reqs[i].remarks, "Approved");
        if (fh_save_requests(reqs, rn)) printf("Request #%d approved.\n", id);
    }
    free(items); free(reqs);
}

void reject_request(void)
{
    if (!require_role(ROLE_ADMIN)) return;
    int rn, i, id;
    Request *reqs = fh_load_requests(&rn);

    list_requests(REQ_PENDING);
    if (rn == 0) { free(reqs); return; }

    id = read_int("Request ID to reject (0 to cancel): ", 0, 100000000);
    if (id == 0) { free(reqs); return; }
    for (i = 0; i < rn && reqs[i].req_id != id; i++) ;
    if (i == rn || reqs[i].status != REQ_PENDING) {
        printf("No pending request with that ID.\n"); free(reqs); return;
    }
    read_line("Reason for rejection: ", reqs[i].remarks, sizeof reqs[i].remarks);
    if (reqs[i].remarks[0] == '\0') strcpy(reqs[i].remarks, "Rejected");
    reqs[i].status = REQ_REJECTED;
    if (fh_save_requests(reqs, rn)) printf("Request #%d rejected.\n", id);
    free(reqs);
}

void requests_menu(int role)
{
    int c;
    if (role == ROLE_ADMIN) {
        do {
            printf("\n===== REQUESTS (REVIEW) =====\n"
                   "1. View All Requests\n2. Approve\n3. Reject\n0. Back\n");
            c = read_int("Choice: ", 0, 3);
            switch (c) {
                case 1: list_requests(-1);  break;
                case 2: approve_request();  break;
                case 3: reject_request();   break;
            }
        } while (c != 0);
    } else {
        do {
            printf("\n===== REQUESTS =====\n"
                   "1. New Request\n2. View Request Status\n0. Back\n");
            c = read_int("Choice: ", 0, 2);
            switch (c) {
                case 1: create_request();   break;
                case 2: list_requests(-1);  break;
            }
        } while (c != 0);
    }
}
