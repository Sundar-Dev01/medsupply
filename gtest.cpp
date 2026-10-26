#include <gtest/gtest.h>

extern "C" {
#include "medsupply.h"
}

/* =====================================================
   1. UTILS FUNCTIONS EDGE CASES (utils.c)[cite: 9]
   ===================================================== */

/* --- valid_date() edge cases --- */
TEST(UtilsValidDateTest, ValidBoundaryDates)
{
    EXPECT_EQ(1, valid_date("2000-01-01")); // Lower year bound[cite: 9]
    EXPECT_EQ(1, valid_date("2100-12-31")); // Upper year bound[cite: 9]
    EXPECT_EQ(1, valid_date("2026-06-15")); // Normal valid date[cite: 9]
}

TEST(UtilsValidDateTest, InvalidLength)
{
    EXPECT_EQ(0, valid_date(""));          // Empty string[cite: 9]
    EXPECT_EQ(0, valid_date("2026-06-1"));    // Too short[cite: 9]
    EXPECT_EQ(0, valid_date("2026-06-155"));  // Too long[cite: 9]
}

TEST(UtilsValidDateTest, InvalidSeparators)
{
    EXPECT_EQ(0, valid_date("2026/06/15")); // Slash separator[cite: 9]
    EXPECT_EQ(0, valid_date("2026.06.15")); // Dot separator[cite: 9]
    EXPECT_EQ(0, valid_date("20260615"));   // No separators[cite: 9]
}

TEST(UtilsValidDateTest, NonDigitCharacters)
{
    EXPECT_EQ(0, valid_date("20a6-06-15")); // Letter in year[cite: 9]
    EXPECT_EQ(0, valid_date("2026-ab-15")); // Letters in month[cite: 9]
    EXPECT_EQ(0, valid_date("2026-06-1x")); // Letter in day[cite: 9]
}

TEST(UtilsValidDateTest, OutOfRangeValues)
{
    EXPECT_EQ(0, valid_date("1999-12-31")); // Year below 2000[cite: 9]
    EXPECT_EQ(0, valid_date("2101-01-01")); // Year above 2100[cite: 9]
    EXPECT_EQ(0, valid_date("2026-00-15")); // Month below 1[cite: 9]
    EXPECT_EQ(0, valid_date("2026-13-15")); // Month above 12[cite: 9]
    EXPECT_EQ(0, valid_date("2026-06-00")); // Day below 1[cite: 9]
    EXPECT_EQ(0, valid_date("2026-06-32")); // Day above 31[cite: 9]
}

/* --- days_until() edge cases --- */
TEST(UtilsDaysUntilTest, FutureAndPastDates)
{
    EXPECT_GT(days_until("2030-01-01"), 0); // Future date[cite: 9]
    EXPECT_LT(days_until("2020-01-01"), 0); // Past/expired date[cite: 9]
}

/* --- contains_ci() edge cases --- */
TEST(UtilsContainsCITest, MatchesAndNonMatches)
{
    EXPECT_EQ(1, contains_ci("Paracetamol 500mg", "Paracetamol")); // Exact match[cite: 9]
    EXPECT_EQ(1, contains_ci("Ibuprofen Tablets", "ibuprofen"));     // Case-insensitive match[cite: 9]
    EXPECT_EQ(1, contains_ci("Surgical Gloves", ""));             // Empty key[cite: 9]
    EXPECT_EQ(0, contains_ci("", "Syringe"));                     // Empty text[cite: 9]
    EXPECT_EQ(0, contains_ci("Masks", "Ventilator"));             // No match[cite: 9]
    EXPECT_EQ(0, contains_ci("Short", "LongerKeyTest"));          // Key longer than text[cite: 9]
}


/* =====================================================
   2. AUTH FUNCTIONS EDGE CASES (auth.c)[cite: 1]
   ===================================================== */

/* --- role_name() edge cases --- */
TEST(AuthRoleNameTest, RoleMappings)
{
    EXPECT_STREQ("ADMIN", role_name(ROLE_ADMIN)); // Admin role[cite: 1, 6]
    EXPECT_STREQ("STAFF", role_name(ROLE_STAFF)); // Staff role[cite: 1, 6]
    EXPECT_STREQ("UNKNOWN", role_name(0));        // Invalid role (zero)[cite: 1]
    EXPECT_STREQ("UNKNOWN", role_name(99));       // Invalid role (arbitrary)[cite: 1]
    EXPECT_STREQ("UNKNOWN", role_name(-1));       // Negative role[cite: 1]
}

/* --- require_role() edge cases --- */
TEST(AuthRequireRoleTest, PrivilegeValidation)
{
    g_current_user.role = ROLE_ADMIN;
    EXPECT_EQ(1, require_role(ROLE_ADMIN)); // Matching admin role[cite: 1, 6]
    EXPECT_EQ(0, require_role(ROLE_STAFF)); // Mismatched staff role requirement[cite: 1, 6]

    g_current_user.role = ROLE_STAFF;
    EXPECT_EQ(1, require_role(ROLE_STAFF)); // Matching staff role[cite: 1, 6]
    EXPECT_EQ(0, require_role(ROLE_ADMIN)); // Mismatched admin role requirement[cite: 1, 6]
}


/* =====================================================
   3. INVENTORY FUNCTIONS EDGE CASES (inventory.c)[cite: 4]
   ===================================================== */

/* --- find_item_index() edge cases --- */
TEST(InventoryFindIndexTest, IndexScenarios)
{
    Item items[3] = {
        {101, "Masks", "PPE", 50, 1.5f, "2028-01-01", 10},
        {102, "Syringes", "Equipment", 100, 0.5f, "2029-05-10", 20},
        {103, "Saline", "Fluids", 200, 2.0f, "2027-11-20", 30}
    };

    EXPECT_EQ(0, find_item_index(items, 3, 101)); // First element[cite: 4]
    EXPECT_EQ(1, find_item_index(items, 3, 102)); // Middle element[cite: 4]
    EXPECT_EQ(2, find_item_index(items, 3, 103)); // Last element[cite: 4]
    EXPECT_EQ(-1, find_item_index(items, 3, 999)); // Non-existent ID[cite: 4]
    EXPECT_EQ(-1, find_item_index(items, 0, 101)); // Empty array (n = 0)[cite: 4]
}


/* =====================================================
   4. REQUESTS FUNCTIONS EDGE CASES (requests.c)[cite: 7]
   ===================================================== */

/* --- status_name() edge cases --- */
TEST(RequestsStatusNameTest, StatusMappings)
{
    EXPECT_STREQ("PENDING", status_name(REQ_PENDING));   // Pending status[cite: 6, 7]
    EXPECT_STREQ("APPROVED", status_name(REQ_APPROVED)); // Approved status[cite: 6, 7]
    EXPECT_STREQ("REJECTED", status_name(REQ_REJECTED)); // Rejected status[cite: 6, 7]
    EXPECT_STREQ("ISSUED", status_name(REQ_ISSUED));     // Issued status[cite: 6, 7]
    EXPECT_STREQ("UNKNOWN", status_name(-1));            // Invalid negative status[cite: 7]
    EXPECT_STREQ("UNKNOWN", status_name(99));            // Invalid out-of-range status[cite: 7]
}

/* --- reserved_qty() edge cases --- */
TEST(RequestsReservedQtyTest, QuantityCalculations)
{
    Request reqs[4] = {
        {1, 101, "WardA", 10, REQ_APPROVED, "2026-06-01", "", "staff1"},
        {2, 101, "WardB", 15, REQ_PENDING,  "2026-06-02", "", "staff2"}, // Should not be counted (PENDING)
        {3, 101, "WardC", 20, REQ_APPROVED, "2026-06-03", "", "staff1"},
        {4, 102, "WardD", 50, REQ_APPROVED, "2026-06-04", "", "staff2"}  // Different item ID
    };

    // Only APPROVED requests for item 101 (reqs 1 and 3): 10 + 20 = 30[cite: 7]
    EXPECT_EQ(30, reserved_qty(reqs, 4, 101));
    // Item 102 has 50 approved[cite: 7]
    EXPECT_EQ(50, reserved_qty(reqs, 4, 102));
    // Item with no requests[cite: 7]
    EXPECT_EQ(0, reserved_qty(reqs, 4, 999));
    // Empty array[cite: 7]
    EXPECT_EQ(0, reserved_qty(reqs, 0, 101));
}


/* =====================================================
   5. FILE HANDLER FUNCTIONS EDGE CASES (filehandler.c)[cite: 3]
   ===================================================== */

TEST(FileHandlerTest, SaveAndLoadItemsEdges)
{
    // Edge case: Save empty item array (count = 0)[cite: 3]
    EXPECT_EQ(1, fh_save_items(nullptr, 0));

    int count = 0;
    Item *loaded = fh_load_items(&count);
    // Loading when file might be empty or missing should handle gracefully (returns NULL or empty count)[cite: 3]
    if (loaded != nullptr) {
        free(loaded);
    }

    // Normal save and load single item[cite: 3]
    Item item = {100, "TestItem", "PPE", 10, 5.0f, "2028-01-01", 2};
    EXPECT_EQ(1, fh_save_items(&item, 1));

    loaded = fh_load_items(&count);
    ASSERT_NE(nullptr, loaded);
    EXPECT_EQ(1, count);
    EXPECT_EQ(100, loaded[0].id);
    free(loaded);
}

TEST(FileHandlerTest, SaveAndLoadRequestsEdges)
{
    // Edge case: Save 0 requests[cite: 3]
    EXPECT_EQ(1, fh_save_requests(nullptr, 0));

    int count = 0;
    Request req = {1, 100, "WardA", 5, REQ_PENDING, "2026-06-15", "", "admin"};
    EXPECT_EQ(1, fh_save_requests(&req, 1));

    Request *loaded = fh_load_requests(&count);
    ASSERT_NE(nullptr, loaded);
    EXPECT_EQ(1, count);
    EXPECT_EQ(100, loaded[0].item_id);
    free(loaded);
}

TEST(FileHandlerTest, SaveAndLoadUsersEdges)
{
    int count = 0;
    User user;
    memset(&user, 0, sizeof(user));
    snprintf(user.username, sizeof(user.username), "admin");
    user.role = ROLE_ADMIN;

    EXPECT_EQ(1, fh_save_users(&user, 1));

    User *loaded = fh_load_users(&count);
    ASSERT_NE(nullptr, loaded);
    EXPECT_EQ(1, count);
    EXPECT_STREQ("admin", loaded[0].username);
    free(loaded);
}

TEST(FileHandlerTest, HistoryAppendAndLoadEdges)
{
    int count = 0;
    Distribution dist = {
        .dist_id = 1,
        .req_id = 10,
        .item_id = 101,
        .qty = 5
    };
    snprintf(dist.item_name, sizeof(dist.item_name), "Masks");
    snprintf(dist.recipient, sizeof(dist.recipient), "ICU");
    snprintf(dist.date, sizeof(dist.date), "2026-06-15");
    snprintf(dist.issued_by, sizeof(dist.issued_by), "admin");

    // Append history record[cite: 3]
    EXPECT_EQ(1, fh_append_history(&dist));

    Distribution *loaded = fh_load_history(&count);
    ASSERT_NE(nullptr, loaded);
    EXPECT_GT(count, 0);
    EXPECT_EQ(10, loaded[count - 1].req_id);
    free(loaded);
}

/* =====================================================
   MAIN FUNCTION
   ===================================================== */

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}