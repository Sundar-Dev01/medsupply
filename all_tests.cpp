/*
 * test_medsupply.cpp  -  GoogleTest suite for the Medical Supply Management POC
 *
 * Covers every non-static function in:
 *   utils.c, filehandler.c, auth.c, inventory.c, stock.c, requests.c, distribution.c
 * (and, optionally, main.c - see below).
 *
 * HOW THE INTERACTIVE FUNCTIONS ARE TESTED
 *   - stdin  is replaced with a temp file  (SetInput / RunWith helpers)
 *   - stdout is captured with testing::internal::CaptureStdout()
 *   - functions that call exit(0) on EOF are tested with EXPECT_EXIT (death tests)
 *   - every test starts and ends with the *.dat files deleted (fixture)
 *   - dates are generated relative to "today", so tests never go stale
 *
 * BUILD (Linux / MinGW, with coverage)
 *   gcc -c --coverage -Wall auth.c distribution.c filehandler.c inventory.c \
 *                     requests.c stock.c utils.c
 *   g++ -std=c++17 --coverage test_medsupply.cpp *.o -lgtest -lpthread -o run_tests
 *   ./run_tests
 *   gcovr -r . --html-details -o coverage.html        (or lcov / gcov)
 *
 *   NOTE: this file has its own main(), so main.c must NOT be linked as-is.
 *
 * OPTIONAL - also cover main.c (admin_menu / staff_menu / main):
 *   gcc -c --coverage -Dmain=app_main main.c -o main_app.o
 *   g++ -std=c++17 --coverage -DCOVER_MAIN_C test_medsupply.cpp *.o -lgtest -lpthread -o run_tests
 *
 * If your gtest is older than 1.10, replace INSTANTIATE_TEST_SUITE_P with
 * INSTANTIATE_TEST_CASE_P.
 */
#include <gtest/gtest.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <direct.h>
  #define MAKE_DIR(p)   _mkdir(p)
  #define REMOVE_DIR(p) _rmdir(p)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
  #define MAKE_DIR(p)   mkdir((p), 0755)
  #define REMOVE_DIR(p) rmdir(p)
#endif

extern "C"
{
#include "medsupply.h"
}

#ifdef COVER_MAIN_C
extern "C" int app_main(void);   /* main.c compiled with -Dmain=app_main */
#endif

/*=====================================================
  Test helpers
=====================================================*/
static const char *kStdinFile = "gts_stdin.tmp";
static const char *kDataFiles[] = { ITEMS_FILE, REQUESTS_FILE, HISTORY_FILE, USERS_FILE };

#define EXPECT_HAS(text, needle) \
    EXPECT_NE(std::string::npos, (text).find(needle)) \
        << "Expected to find: " << (needle) << "\n--- output ---\n" << (text)
#define EXPECT_LACKS(text, needle) \
    EXPECT_EQ(std::string::npos, (text).find(needle)) \
        << "Did not expect: " << (needle) << "\n--- output ---\n" << (text)

/* Joins lines with '\n' (each line terminated) - simulates typed input. */
static std::string Lines(const std::vector<std::string> &v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); i++) { s += v[i]; s += '\n'; }
    return s;
}

/* Replace stdin with the given text. */
static void SetInput(const std::string &text)
{
    FILE *f = fopen(kStdinFile, "wb");
    if (f) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
    FILE *r = freopen(kStdinFile, "r", stdin);
    (void)r;
}

/* Feed `input` to stdin, run fn(), return everything fn() printed. */
template <typename Fn>
static std::string RunWith(const std::string &input, Fn fn)
{
    SetInput(input);
    testing::internal::CaptureStdout();
    fn();
    return testing::internal::GetCapturedStdout();
}

/* "YYYY-MM-DD" for today + `days` (negative = past). */
static std::string DateOffset(int days)
{
    time_t now = time(NULL);
    struct tm t = *localtime(&now);
    t.tm_mday += days;
    t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    mktime(&t);
    char buf[16];
    strftime(buf, sizeof buf, "%Y-%m-%d", &t);
    return buf;
}

static void SetCurrentUser(const char *name, int role)
{
    memset(&g_current_user, 0, sizeof g_current_user);
    snprintf(g_current_user.username, sizeof g_current_user.username, "%s", name);
    g_current_user.role = role;
}

/* ---- record builders ---- */
static Item MakeItem(int id, const char *name, const char *cat, int qty,
                     float price, const std::string &expiry, int minLevel)
{
    Item it;
    memset(&it, 0, sizeof it);
    it.id = id;
    snprintf(it.name, sizeof it.name, "%s", name);
    snprintf(it.category, sizeof it.category, "%s", cat);
    it.quantity = qty;
    it.price = price;
    snprintf(it.expiry, sizeof it.expiry, "%s", expiry.c_str());
    it.min_level = minLevel;
    return it;
}

static Request MakeReq(int id, int itemId, const char *who, int qty, int status,
                       const char *remarks = "", const char *by = "staff1")
{
    Request r;
    memset(&r, 0, sizeof r);
    r.req_id = id;
    r.item_id = itemId;
    snprintf(r.requester, sizeof r.requester, "%s", who);
    r.qty = qty;
    r.status = status;
    snprintf(r.date, sizeof r.date, "%s", "2026-01-01");
    snprintf(r.remarks, sizeof r.remarks, "%s", remarks);
    snprintf(r.created_by, sizeof r.created_by, "%s", by);
    return r;
}

static Distribution MakeDist(int distId, int reqId, int itemId, const char *itemName,
                             const char *recipient, int qty, const char *by = "staff1")
{
    Distribution d;
    memset(&d, 0, sizeof d);
    d.dist_id = distId;
    d.req_id = reqId;
    d.item_id = itemId;
    snprintf(d.item_name, sizeof d.item_name, "%s", itemName);
    snprintf(d.recipient, sizeof d.recipient, "%s", recipient);
    d.qty = qty;
    snprintf(d.date, sizeof d.date, "%s", "2026-01-01");
    snprintf(d.issued_by, sizeof d.issued_by, "%s", by);
    return d;
}

/* ---- seed / load helpers ---- */
static void SeedItems(const std::vector<Item> &v)
{
    ASSERT_EQ(1, fh_save_items(v.empty() ? NULL : &v[0], (int)v.size()));
}
static void SeedRequests(const std::vector<Request> &v)
{
    ASSERT_EQ(1, fh_save_requests(v.empty() ? NULL : &v[0], (int)v.size()));
}
static void SeedHistory(const std::vector<Distribution> &v)
{
    for (size_t i = 0; i < v.size(); i++) ASSERT_EQ(1, fh_append_history(&v[i]));
}

static std::vector<Item> LoadItems()
{
    int n = 0; Item *p = fh_load_items(&n);
    std::vector<Item> v;
    if (p) { v.assign(p, p + n); free(p); }
    return v;
}
static std::vector<Request> LoadRequests()
{
    int n = 0; Request *p = fh_load_requests(&n);
    std::vector<Request> v;
    if (p) { v.assign(p, p + n); free(p); }
    return v;
}
static std::vector<User> LoadUsers()
{
    int n = 0; User *p = fh_load_users(&n);
    std::vector<User> v;
    if (p) { v.assign(p, p + n); free(p); }
    return v;
}
static std::vector<Distribution> LoadHistory()
{
    int n = 0; Distribution *p = fh_load_history(&n);
    std::vector<Distribution> v;
    if (p) { v.assign(p, p + n); free(p); }
    return v;
}

static void WriteRaw(const char *path, const char *mode, size_t nBytes)
{
    FILE *f = fopen(path, mode);
    if (f) { std::string s(nBytes, 'x'); fwrite(s.data(), 1, s.size(), f); fclose(f); }
}

/* ---- auth helpers ---- */
static void InitAuth()
{
    fh_init_files();
    auth_init();                       /* creates admin/admin123 */
}

/* Creates a user by driving the admin "Add User" menu. */
static void AddUserViaMenu(const char *name, const char *pw, int role)
{
    InitAuth();
    User saved = g_current_user;
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "2", name, pw, pw, role == ROLE_ADMIN ? "2" : "1", "0" }),
            [] { user_management_menu(); });
    g_current_user = saved;
}

static int Login(const char *user, const char *pw, std::string *outText = NULL)
{
    int ok = -1;
    std::string out = RunWith(Lines({ user, pw }), [&] { ok = auth_login(); });
    if (outText) *outText = out;
    return ok;
}

/*=====================================================
  Fixtures
=====================================================*/
class MedSupplyTest : public ::testing::Test
{
protected:
    static void CleanFiles()
    {
        for (size_t i = 0; i < sizeof(kDataFiles) / sizeof(kDataFiles[0]); i++)
        {
            remove(kDataFiles[i]);
            REMOVE_DIR(kDataFiles[i]);      /* in case a test made it a directory */
        }
    }
    void SetUp() override
    {
        CleanFiles();
        memset(&g_current_user, 0, sizeof g_current_user);
    }
    void TearDown() override
    {
        CleanFiles();
        remove(kStdinFile);
    }
};

class UtilsTest        : public MedSupplyTest {};
class FileHandlerTest  : public MedSupplyTest {};
class AuthTest         : public MedSupplyTest {};
class InventoryTest    : public MedSupplyTest {};
class StockTest        : public MedSupplyTest {};
class RequestsTest     : public MedSupplyTest {};
class DistributionTest : public MedSupplyTest {};
class InputEofDeathTest: public MedSupplyTest {};
#ifdef COVER_MAIN_C
class MainMenuTest     : public MedSupplyTest {};
#endif

/*#####################################################
  utils.c
#####################################################*/

/*=====================================================
  read_line()
=====================================================*/
TEST_F(UtilsTest, ReadLine_StripsNewlineAndPrintsPrompt)
{
    char buf[32];
    std::string out = RunWith("hello\n", [&] { read_line("Name: ", buf, sizeof buf); });
    EXPECT_STREQ("hello", buf);
    EXPECT_HAS(out, "Name: ");
}

TEST_F(UtilsTest, ReadLine_EmptyLine)
{
    char buf[32] = "junk";
    RunWith("\n", [&] { read_line("p", buf, sizeof buf); });
    EXPECT_STREQ("", buf);
}

TEST_F(UtilsTest, ReadLine_NullPromptPrintsNothing)
{
    char buf[32];
    std::string out = RunWith("abc\n", [&] { read_line(NULL, buf, sizeof buf); });
    EXPECT_STREQ("abc", buf);
    EXPECT_EQ("", out);
}

TEST_F(UtilsTest, ReadLine_LongInputTruncatedAndRestFlushed)
{
    char a[8], b[8];
    RunWith("ABCDEFGHIJKLMNOP\nnext\n", [&] {
        read_line(NULL, a, sizeof a);
        read_line(NULL, b, sizeof b);
    });
    EXPECT_STREQ("ABCDEFG", a);
    EXPECT_STREQ("next", b);
}

TEST_F(UtilsTest, ReadLine_ExactFitInputDoesNotLeakNewlineIntoNextRead)
{
    char a[8], b[8];
    RunWith("ABCDEFG\nnext\n", [&] {
        read_line(NULL, a, sizeof a);
        read_line(NULL, b, sizeof b);
    });
    EXPECT_STREQ("ABCDEFG", a);
    EXPECT_STREQ("next", b);
}

TEST_F(UtilsTest, ReadLine_LastLineWithoutNewline)
{
    char buf[16];
    RunWith("abc", [&] { read_line(NULL, buf, sizeof buf); });
    EXPECT_STREQ("abc", buf);
}

/*=====================================================
  read_int()
=====================================================*/
TEST_F(UtilsTest, ReadInt_ValidNumber)
{
    int v = 0;
    RunWith("42\n", [&] { v = read_int("N: ", 0, 100); });
    EXPECT_EQ(42, v);
}

TEST_F(UtilsTest, ReadInt_MinBoundaryAccepted)
{
    int v = -1;
    RunWith("5\n", [&] { v = read_int("N: ", 5, 10); });
    EXPECT_EQ(5, v);
}

TEST_F(UtilsTest, ReadInt_MaxBoundaryAccepted)
{
    int v = -1;
    RunWith("10\n", [&] { v = read_int("N: ", 5, 10); });
    EXPECT_EQ(10, v);
}

TEST_F(UtilsTest, ReadInt_BelowMinReprompts)
{
    int v = 0;
    std::string out = RunWith("4\n7\n", [&] { v = read_int("N: ", 5, 10); });
    EXPECT_EQ(7, v);
    EXPECT_HAS(out, "Invalid input. Enter a number between 5 and 10.");
}

TEST_F(UtilsTest, ReadInt_AboveMaxReprompts)
{
    int v = 0;
    std::string out = RunWith("11\n10\n", [&] { v = read_int("N: ", 5, 10); });
    EXPECT_EQ(10, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadInt_NonNumericReprompts)
{
    int v = 0;
    std::string out = RunWith("abc\n3\n", [&] { v = read_int("N: ", 0, 5); });
    EXPECT_EQ(3, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadInt_TrailingGarbageRejected)
{
    int v = 0;
    std::string out = RunWith("5x\n6\n", [&] { v = read_int("N: ", 0, 10); });
    EXPECT_EQ(6, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadInt_EmptyLineRejected)
{
    int v = 0;
    std::string out = RunWith("\n2\n", [&] { v = read_int("N: ", 0, 10); });
    EXPECT_EQ(2, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadInt_NegativeRange)
{
    int v = 0;
    RunWith("-3\n", [&] { v = read_int("N: ", -5, 5); });
    EXPECT_EQ(-3, v);
}

TEST_F(UtilsTest, ReadInt_LeadingWhitespaceAndPlusSignAccepted)
{
    int a = 0, b = 0;
    RunWith("  7\n+8\n", [&] { a = read_int("N: ", 0, 10); b = read_int("N: ", 0, 10); });
    EXPECT_EQ(7, a);
    EXPECT_EQ(8, b);
}

TEST_F(UtilsTest, ReadInt_TrailingSpaceRejected)
{
    int v = 0;
    std::string out = RunWith("7 \n8\n", [&] { v = read_int("N: ", 0, 10); });
    EXPECT_EQ(8, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadInt_HugeNumberRejected)
{
    int v = 0;
    std::string out = RunWith("99999999999999999999\n1\n", [&] { v = read_int("N: ", 0, 10); });
    EXPECT_EQ(1, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadInt_HexPrefixRejected)
{
    int v = 0;
    std::string out = RunWith("0x10\n4\n", [&] { v = read_int("N: ", 0, 100); });
    EXPECT_EQ(4, v);
    EXPECT_HAS(out, "Invalid input");
}

/*=====================================================
  read_float()
=====================================================*/
TEST_F(UtilsTest, ReadFloat_ValidDecimal)
{
    float v = 0;
    RunWith("3.5\n", [&] { v = read_float("F: ", 0.0f, 10.0f); });
    EXPECT_FLOAT_EQ(3.5f, v);
}

TEST_F(UtilsTest, ReadFloat_IntegerTextAccepted)
{
    float v = 0;
    RunWith("7\n", [&] { v = read_float("F: ", 0.0f, 10.0f); });
    EXPECT_FLOAT_EQ(7.0f, v);
}

TEST_F(UtilsTest, ReadFloat_ScientificNotationAccepted)
{
    float v = 0;
    RunWith("1e2\n", [&] { v = read_float("F: ", 0.0f, 1000.0f); });
    EXPECT_FLOAT_EQ(100.0f, v);
}

TEST_F(UtilsTest, ReadFloat_BoundariesAccepted)
{
    float lo = -1, hi = -1;
    RunWith("0\n10\n", [&] { lo = read_float("F: ", 0.0f, 10.0f); hi = read_float("F: ", 0.0f, 10.0f); });
    EXPECT_FLOAT_EQ(0.0f, lo);
    EXPECT_FLOAT_EQ(10.0f, hi);
}

TEST_F(UtilsTest, ReadFloat_BelowMinReprompts)
{
    float v = 0;
    std::string out = RunWith("-1\n2.5\n", [&] { v = read_float("F: ", 0.0f, 10.0f); });
    EXPECT_FLOAT_EQ(2.5f, v);
    EXPECT_HAS(out, "Invalid input. Enter a value between 0.00 and 10.00.");
}

TEST_F(UtilsTest, ReadFloat_AboveMaxReprompts)
{
    float v = 0;
    std::string out = RunWith("10.01\n9\n", [&] { v = read_float("F: ", 0.0f, 10.0f); });
    EXPECT_FLOAT_EQ(9.0f, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadFloat_NonNumericAndEmptyAndTrailingGarbageRejected)
{
    float v = 0;
    std::string out = RunWith("abc\n\n2.5kg\n4.25\n", [&] { v = read_float("F: ", 0.0f, 10.0f); });
    EXPECT_FLOAT_EQ(4.25f, v);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(UtilsTest, ReadFloat_NanAndInfRejected)
{
    float v = 0;
    std::string out = RunWith("nan\ninf\n1\n", [&] { v = read_float("F: ", 0.0f, 10.0f); });
    EXPECT_FLOAT_EQ(1.0f, v);
    EXPECT_HAS(out, "Invalid input");
}

/*=====================================================
  confirm()
=====================================================*/
TEST_F(UtilsTest, Confirm_LowerCaseYes)
{
    int r = 0;
    RunWith("y\n", [&] { r = confirm("ok? "); });
    EXPECT_EQ(1, r);
}

TEST_F(UtilsTest, Confirm_UpperCaseYes)
{
    int r = 0;
    RunWith("Y\n", [&] { r = confirm("ok? "); });
    EXPECT_EQ(1, r);
}

TEST_F(UtilsTest, Confirm_WordStartingWithYIsYes)
{
    int r = 0;
    RunWith("yes\n", [&] { r = confirm("ok? "); });
    EXPECT_EQ(1, r);
}

TEST_F(UtilsTest, Confirm_NoVariantsReturnZero)
{
    int a = -1, b = -1, c = -1, d = -1;
    RunWith("n\nN\nmaybe\n\n", [&] {
        a = confirm("? "); b = confirm("? "); c = confirm("? "); d = confirm("? ");
    });
    EXPECT_EQ(0, a);
    EXPECT_EQ(0, b);
    EXPECT_EQ(0, c);
    EXPECT_EQ(0, d);
}

/*=====================================================
  get_today()
=====================================================*/
TEST_F(UtilsTest, GetToday_HasIsoFormat)
{
    char today[11];
    get_today(today);
    ASSERT_EQ(10u, strlen(today));
    EXPECT_EQ('-', today[4]);
    EXPECT_EQ('-', today[7]);
    EXPECT_EQ(1, valid_date(today));
}

TEST_F(UtilsTest, GetToday_MatchesSystemDate)
{
    char today[11];
    get_today(today);
    EXPECT_EQ(DateOffset(0), std::string(today));
}

/*=====================================================
  valid_date()   (data driven)
=====================================================*/
struct DateCase { const char *text; int expected; };

class ValidDateParamTest : public ::testing::TestWithParam<DateCase> {};

TEST_P(ValidDateParamTest, Classifies)
{
    EXPECT_EQ(GetParam().expected, valid_date(GetParam().text)) << "input: " << GetParam().text;
}

INSTANTIATE_TEST_SUITE_P(Dates, ValidDateParamTest, ::testing::Values(
    DateCase{"2027-12-31", 1},
    DateCase{"2000-01-01", 1},      /* lowest year        */
    DateCase{"2100-12-31", 1},      /* highest year       */
    DateCase{"2026-06-15", 1},
    DateCase{"1999-12-31", 0},      /* year too low       */
    DateCase{"2101-01-01", 0},      /* year too high      */
    DateCase{"2027-00-10", 0},      /* month 0            */
    DateCase{"2027-13-10", 0},      /* month 13           */
    DateCase{"2027-15-31", 0},
    DateCase{"2027-12-00", 0},      /* day 0              */
    DateCase{"2027-12-32", 0},      /* day 32             */
    DateCase{"31-12-2027", 0},      /* wrong order        */
    DateCase{"2027/12/31", 0},      /* wrong separator    */
    DateCase{"2027-1-31",  0},      /* too short          */
    DateCase{"2027-12-311", 0},     /* too long           */
    DateCase{"abcd-ef-gh", 0},      /* non digits         */
    DateCase{"2027-1a-31", 0},      /* letter inside      */
    DateCase{"",           0},      /* empty              */
    DateCase{"2027-02-31", 1}       /* current behaviour: no per-month day check */
));

/*=====================================================
  days_until()
=====================================================*/
TEST_F(UtilsTest, DaysUntil_TodayIsZero)       { EXPECT_EQ(0,   days_until(DateOffset(0).c_str())); }
TEST_F(UtilsTest, DaysUntil_TomorrowIsOne)     { EXPECT_EQ(1,   days_until(DateOffset(1).c_str())); }
TEST_F(UtilsTest, DaysUntil_YesterdayIsMinus1) { EXPECT_EQ(-1,  days_until(DateOffset(-1).c_str())); }
TEST_F(UtilsTest, DaysUntil_ThirtyDaysAhead)   { EXPECT_EQ(30,  days_until(DateOffset(30).c_str())); }
TEST_F(UtilsTest, DaysUntil_ThirtyDaysAgo)     { EXPECT_EQ(-30, days_until(DateOffset(-30).c_str())); }
TEST_F(UtilsTest, DaysUntil_AcrossDstBoundary) { EXPECT_EQ(180, days_until(DateOffset(180).c_str())); }
TEST_F(UtilsTest, DaysUntil_OneYearAhead)      { EXPECT_EQ(365, days_until(DateOffset(365).c_str())); }
TEST_F(UtilsTest, DaysUntil_FixedFarFuture)    { EXPECT_GT(days_until("2099-01-01"), 0); }
TEST_F(UtilsTest, DaysUntil_FixedFarPast)      { EXPECT_LT(days_until("2001-01-01"), 0); }

/*=====================================================
  contains_ci()   (data driven)
=====================================================*/
struct ContainsCase { const char *text; const char *key; int expected; };

class ContainsCiParamTest : public ::testing::TestWithParam<ContainsCase> {};

TEST_P(ContainsCiParamTest, Classifies)
{
    EXPECT_EQ(GetParam().expected, contains_ci(GetParam().text, GetParam().key))
        << "text=" << GetParam().text << " key=" << GetParam().key;
}

INSTANTIATE_TEST_SUITE_P(Keys, ContainsCiParamTest, ::testing::Values(
    ContainsCase{"Paracetamol", "para", 1},
    ContainsCase{"Paracetamol", "PARA", 1},
    ContainsCase{"PARACETAMOL", "para", 1},
    ContainsCase{"Paracetamol", "tamol", 1},        /* at the end      */
    ContainsCase{"Paracetamol", "cet", 1},          /* in the middle   */
    ContainsCase{"Paracetamol", "Paracetamol", 1},  /* exact match     */
    ContainsCase{"Paracetamol", "mask", 0},
    ContainsCase{"Paracetamol", "Paracetamols", 0}, /* key longer      */
    ContainsCase{"Paracetamol", "", 1},             /* empty key       */
    ContainsCase{"", "a", 0},                       /* empty text      */
    ContainsCase{"", "", 1},                        /* both empty      */
    ContainsCase{"aab", "ab", 1},                   /* restart match   */
    ContainsCase{"abcabd", "abd", 1},               /* partial then ok */
    ContainsCase{"abcabc", "abd", 0},
    ContainsCase{"Face Mask N95", "mask n", 1}      /* contains space  */
));

/*#####################################################
  filehandler.c
#####################################################*/

/*=====================================================
  fh_init_files()
=====================================================*/
TEST_F(FileHandlerTest, InitFiles_CreatesAllDataFiles)
{
    fh_init_files();
    for (size_t i = 0; i < sizeof(kDataFiles) / sizeof(kDataFiles[0]); i++)
    {
        FILE *fp = fopen(kDataFiles[i], "rb");
        EXPECT_NE(nullptr, fp) << kDataFiles[i];
        if (fp) fclose(fp);
    }
}

TEST_F(FileHandlerTest, InitFiles_DoesNotTruncateExistingData)
{
    SeedItems({ MakeItem(1, "Mask", "Safety", 5, 1.0f, "2030-01-01", 1) });
    fh_init_files();
    EXPECT_EQ(1u, LoadItems().size());
}

TEST_F(FileHandlerTest, InitFiles_WarnsWhenFileCannotBeCreated)
{
    ASSERT_EQ(0, MAKE_DIR(ITEMS_FILE));            /* a directory blocks fopen("ab") */
    std::string out = RunWith("", [] { fh_init_files(); });
    EXPECT_HAS(out, std::string("Warning: cannot create ") + ITEMS_FILE);
}

/*=====================================================
  load_all() behaviour via the public loaders
=====================================================*/
TEST_F(FileHandlerTest, LoadItems_MissingFileReturnsNull)
{
    int count = 99;
    EXPECT_EQ(nullptr, fh_load_items(&count));
    EXPECT_EQ(0, count);
}

TEST_F(FileHandlerTest, LoadItems_EmptyFileReturnsNull)
{
    fh_init_files();
    int count = 99;
    EXPECT_EQ(nullptr, fh_load_items(&count));
    EXPECT_EQ(0, count);
}

TEST_F(FileHandlerTest, LoadItems_TruncatedRecordReturnsNull)
{
    WriteRaw(ITEMS_FILE, "wb", sizeof(Item) - 1);
    int count = 99;
    EXPECT_EQ(nullptr, fh_load_items(&count));
    EXPECT_EQ(0, count);
}

TEST_F(FileHandlerTest, LoadItems_PartialTrailingRecordIgnored)
{
    SeedItems({ MakeItem(1, "A", "C", 1, 1.0f, "2030-01-01", 1),
                MakeItem(2, "B", "C", 2, 2.0f, "2030-01-01", 1) });
    WriteRaw(ITEMS_FILE, "ab", 3);
    EXPECT_EQ(2u, LoadItems().size());
}

/*=====================================================
  Items
=====================================================*/
TEST_F(FileHandlerTest, SaveAndLoadItems_RoundTripsEveryField)
{
    Item in = MakeItem(100, "Mask", "Safety", 50, 2.5f, "2030-01-01", 10);
    ASSERT_EQ(1, fh_save_items(&in, 1));

    int count = 0;
    Item *out = fh_load_items(&count);
    ASSERT_NE(nullptr, out);
    ASSERT_EQ(1, count);
    EXPECT_EQ(100, out[0].id);
    EXPECT_STREQ("Mask", out[0].name);
    EXPECT_STREQ("Safety", out[0].category);
    EXPECT_EQ(50, out[0].quantity);
    EXPECT_FLOAT_EQ(2.5f, out[0].price);
    EXPECT_STREQ("2030-01-01", out[0].expiry);
    EXPECT_EQ(10, out[0].min_level);
    free(out);
}

TEST_F(FileHandlerTest, SaveAndLoadItems_MultipleRecordsKeepOrder)
{
    SeedItems({ MakeItem(3, "C", "x", 3, 3.0f, "2030-01-01", 1),
                MakeItem(1, "A", "x", 1, 1.0f, "2030-01-01", 1),
                MakeItem(2, "B", "x", 2, 2.0f, "2030-01-01", 1) });
    std::vector<Item> v = LoadItems();
    ASSERT_EQ(3u, v.size());
    EXPECT_EQ(3, v[0].id);
    EXPECT_EQ(1, v[1].id);
    EXPECT_EQ(2, v[2].id);
}

TEST_F(FileHandlerTest, SaveItems_ZeroCountCreatesEmptyFile)
{
    EXPECT_EQ(1, fh_save_items(NULL, 0));
    EXPECT_EQ(0u, LoadItems().size());
}

TEST_F(FileHandlerTest, SaveItems_OverwritesPreviousContent)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1.0f, "2030-01-01", 1),
                MakeItem(2, "B", "x", 1, 1.0f, "2030-01-01", 1),
                MakeItem(3, "C", "x", 1, 1.0f, "2030-01-01", 1) });
    SeedItems({ MakeItem(9, "Z", "x", 1, 1.0f, "2030-01-01", 1) });
    std::vector<Item> v = LoadItems();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(9, v[0].id);
}

TEST_F(FileHandlerTest, SaveItems_FailsWhenTargetIsDirectory)
{
    ASSERT_EQ(0, MAKE_DIR(ITEMS_FILE));
    Item it = MakeItem(1, "A", "x", 1, 1.0f, "2030-01-01", 1);
    int rc = -1;
    std::string out = RunWith("", [&] { rc = fh_save_items(&it, 1); });
    EXPECT_EQ(0, rc);
    EXPECT_HAS(out, "Error: cannot write to");
}

/*=====================================================
  Requests / Users / History
=====================================================*/
TEST_F(FileHandlerTest, SaveAndLoadRequests_RoundTripsEveryField)
{
    Request in = MakeReq(7, 100, "WardA", 5, REQ_PENDING, "note", "staff1");
    ASSERT_EQ(1, fh_save_requests(&in, 1));

    std::vector<Request> v = LoadRequests();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(7, v[0].req_id);
    EXPECT_EQ(100, v[0].item_id);
    EXPECT_STREQ("WardA", v[0].requester);
    EXPECT_EQ(5, v[0].qty);
    EXPECT_EQ(REQ_PENDING, v[0].status);
    EXPECT_STREQ("2026-01-01", v[0].date);
    EXPECT_STREQ("note", v[0].remarks);
    EXPECT_STREQ("staff1", v[0].created_by);
}

TEST_F(FileHandlerTest, LoadRequests_MissingFileReturnsNull)
{
    int count = 5;
    EXPECT_EQ(nullptr, fh_load_requests(&count));
    EXPECT_EQ(0, count);
}

TEST_F(FileHandlerTest, SaveRequests_FailsWhenTargetIsDirectory)
{
    ASSERT_EQ(0, MAKE_DIR(REQUESTS_FILE));
    Request r = MakeReq(1, 1, "W", 1, REQ_PENDING);
    int rc = -1;
    RunWith("", [&] { rc = fh_save_requests(&r, 1); });
    EXPECT_EQ(0, rc);
}

TEST_F(FileHandlerTest, SaveAndLoadUsers_RoundTrip)
{
    User u;
    memset(&u, 0, sizeof u);
    snprintf(u.username, sizeof u.username, "alice");
    snprintf(u.salt, sizeof u.salt, "0123456789abcdef");
    snprintf(u.hash, sizeof u.hash, "fedcba9876543210");
    u.role = ROLE_STAFF;
    ASSERT_EQ(1, fh_save_users(&u, 1));

    std::vector<User> v = LoadUsers();
    ASSERT_EQ(1u, v.size());
    EXPECT_STREQ("alice", v[0].username);
    EXPECT_STREQ("0123456789abcdef", v[0].salt);
    EXPECT_STREQ("fedcba9876543210", v[0].hash);
    EXPECT_EQ(ROLE_STAFF, v[0].role);
}

TEST_F(FileHandlerTest, LoadUsers_MissingFileReturnsNull)
{
    int count = 5;
    EXPECT_EQ(nullptr, fh_load_users(&count));
    EXPECT_EQ(0, count);
}

TEST_F(FileHandlerTest, SaveUsers_FailsWhenTargetIsDirectory)
{
    ASSERT_EQ(0, MAKE_DIR(USERS_FILE));
    User u;
    memset(&u, 0, sizeof u);
    int rc = -1;
    RunWith("", [&] { rc = fh_save_users(&u, 1); });
    EXPECT_EQ(0, rc);
}

TEST_F(FileHandlerTest, AppendAndLoadHistory_SingleRecord)
{
    Distribution d = MakeDist(1, 2, 3, "Mask", "WardA", 4, "staff1");
    ASSERT_EQ(1, fh_append_history(&d));

    std::vector<Distribution> v = LoadHistory();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(1, v[0].dist_id);
    EXPECT_EQ(2, v[0].req_id);
    EXPECT_EQ(3, v[0].item_id);
    EXPECT_STREQ("Mask", v[0].item_name);
    EXPECT_STREQ("WardA", v[0].recipient);
    EXPECT_EQ(4, v[0].qty);
    EXPECT_STREQ("2026-01-01", v[0].date);
    EXPECT_STREQ("staff1", v[0].issued_by);
}

TEST_F(FileHandlerTest, AppendHistory_AccumulatesRecords)
{
    SeedHistory({ MakeDist(1, 1, 1, "A", "X", 1),
                  MakeDist(2, 2, 1, "A", "Y", 2),
                  MakeDist(3, 3, 1, "A", "Z", 3) });
    std::vector<Distribution> v = LoadHistory();
    ASSERT_EQ(3u, v.size());
    EXPECT_EQ(3, v[2].dist_id);
}

TEST_F(FileHandlerTest, LoadHistory_MissingFileReturnsNull)
{
    int count = 5;
    EXPECT_EQ(nullptr, fh_load_history(&count));
    EXPECT_EQ(0, count);
}

TEST_F(FileHandlerTest, AppendHistory_FailsWhenTargetIsDirectory)
{
    ASSERT_EQ(0, MAKE_DIR(HISTORY_FILE));
    Distribution d = MakeDist(1, 1, 1, "A", "X", 1);
    int rc = -1;
    std::string out = RunWith("", [&] { rc = fh_append_history(&d); });
    EXPECT_EQ(0, rc);
    EXPECT_HAS(out, "Error: cannot open");
}

/*#####################################################
  auth.c
#####################################################*/

/*=====================================================
  role_name()
=====================================================*/
TEST_F(AuthTest, RoleName_Admin)   { EXPECT_STREQ("ADMIN",   role_name(ROLE_ADMIN)); }
TEST_F(AuthTest, RoleName_Staff)   { EXPECT_STREQ("STAFF",   role_name(ROLE_STAFF)); }
TEST_F(AuthTest, RoleName_Unknown) { EXPECT_STREQ("UNKNOWN", role_name(999)); }
TEST_F(AuthTest, RoleName_Zero)    { EXPECT_STREQ("UNKNOWN", role_name(0)); }
TEST_F(AuthTest, RoleName_Negative){ EXPECT_STREQ("UNKNOWN", role_name(-1)); }

/*=====================================================
  require_role()
=====================================================*/
TEST_F(AuthTest, RequireRole_AdminAllowedForAdmin)
{
    SetCurrentUser("a", ROLE_ADMIN);
    int r = 0;
    std::string out = RunWith("", [&] { r = require_role(ROLE_ADMIN); });
    EXPECT_EQ(1, r);
    EXPECT_EQ("", out);
}

TEST_F(AuthTest, RequireRole_StaffAllowedForStaff)
{
    SetCurrentUser("s", ROLE_STAFF);
    EXPECT_EQ(1, require_role(ROLE_STAFF));
}

TEST_F(AuthTest, RequireRole_StaffDeniedAdminArea)
{
    SetCurrentUser("s", ROLE_STAFF);
    int r = 1;
    std::string out = RunWith("", [&] { r = require_role(ROLE_ADMIN); });
    EXPECT_EQ(0, r);
    EXPECT_HAS(out, "Access denied: ADMIN privileges required.");
}

TEST_F(AuthTest, RequireRole_AdminDeniedStaffArea)
{
    SetCurrentUser("a", ROLE_ADMIN);
    int r = 1;
    std::string out = RunWith("", [&] { r = require_role(ROLE_STAFF); });
    EXPECT_EQ(0, r);
    EXPECT_HAS(out, "Access denied: STAFF privileges required.");
}

TEST_F(AuthTest, RequireRole_NobodyLoggedInIsDenied)
{
    int r = 1;
    RunWith("", [&] { r = require_role(ROLE_ADMIN); });
    EXPECT_EQ(0, r);
}

/*=====================================================
  read_password()   (non-terminal path)
=====================================================*/
TEST_F(AuthTest, ReadPassword_ReadsLineFromNonTerminalStdin)
{
    char buf[32];
    std::string out = RunWith("secret\n", [&] { read_password("Password: ", buf, sizeof buf); });
    EXPECT_STREQ("secret", buf);
    EXPECT_HAS(out, "Password: ");
}

TEST_F(AuthTest, ReadPassword_RespectsBufferSize)
{
    char buf[4];
    RunWith("abcdefgh\n", [&] { read_password("P: ", buf, sizeof buf); });
    EXPECT_STREQ("abc", buf);
}

/*=====================================================
  auth_init()
=====================================================*/
TEST_F(AuthTest, AuthInit_CreatesDefaultAdmin)
{
    fh_init_files();
    auth_init();
    std::vector<User> u = LoadUsers();
    ASSERT_EQ(1u, u.size());
    EXPECT_STREQ(DEFAULT_ADMIN_USER, u[0].username);
    EXPECT_EQ(ROLE_ADMIN, u[0].role);
}

TEST_F(AuthTest, AuthInit_StoresSaltedHashNotPlainPassword)
{
    InitAuth();
    std::vector<User> u = LoadUsers();
    ASSERT_EQ(1u, u.size());
    ASSERT_EQ(16u, strlen(u[0].salt));
    ASSERT_EQ(16u, strlen(u[0].hash));
    for (int i = 0; i < 16; i++)
    {
        EXPECT_TRUE(isxdigit((unsigned char)u[0].salt[i]));
        EXPECT_TRUE(isxdigit((unsigned char)u[0].hash[i]));
    }
    EXPECT_STRNE(DEFAULT_ADMIN_PASS, u[0].hash);
}

TEST_F(AuthTest, AuthInit_WorksEvenWithoutExistingFile)
{
    auth_init();
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, AuthInit_IsIdempotent)
{
    InitAuth();
    auth_init();
    auth_init();
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, AuthInit_KeepsExistingUsers)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    auth_init();
    EXPECT_EQ(2u, LoadUsers().size());
}

/*=====================================================
  auth_login()
=====================================================*/
TEST_F(AuthTest, Login_DefaultAdminSucceedsAndWarns)
{
    InitAuth();
    std::string out;
    EXPECT_EQ(1, Login("admin", "admin123", &out));
    EXPECT_STREQ("admin", g_current_user.username);
    EXPECT_EQ(ROLE_ADMIN, g_current_user.role);
    EXPECT_HAS(out, "Welcome, admin (ADMIN).");
    EXPECT_HAS(out, "default admin password is still in use");
}

TEST_F(AuthTest, Login_UsernameIsCaseInsensitive)
{
    InitAuth();
    EXPECT_EQ(1, Login("ADMIN", "admin123"));
    EXPECT_STREQ("admin", g_current_user.username);
}

TEST_F(AuthTest, Login_PasswordIsCaseSensitive)
{
    InitAuth();
    int ok = -1;
    RunWith(Lines({ "admin", "ADMIN123", "admin", "ADMIN123", "admin", "ADMIN123" }),
            [&] { ok = auth_login(); });
    EXPECT_EQ(0, ok);
}

TEST_F(AuthTest, Login_WrongPasswordThenCorrectSucceeds)
{
    InitAuth();
    int ok = -1;
    std::string out = RunWith(Lines({ "admin", "wrongpw", "admin", "admin123" }),
                              [&] { ok = auth_login(); });
    EXPECT_EQ(1, ok);
    EXPECT_HAS(out, "Invalid username or password. (1/3)");
}

TEST_F(AuthTest, Login_UnknownUserFailsAfterMaxAttempts)
{
    InitAuth();
    int ok = -1;
    std::string out = RunWith(Lines({ "ghost", "x", "ghost", "x", "ghost", "x" }),
                              [&] { ok = auth_login(); });
    EXPECT_EQ(0, ok);
    EXPECT_HAS(out, "(1/3)");
    EXPECT_HAS(out, "(2/3)");
    EXPECT_HAS(out, "(3/3)");
}

TEST_F(AuthTest, Login_WrongPasswordThreeTimesFails)
{
    InitAuth();
    int ok = -1;
    RunWith(Lines({ "admin", "a", "admin", "b", "admin", "c" }), [&] { ok = auth_login(); });
    EXPECT_EQ(0, ok);
}

TEST_F(AuthTest, Login_NoUsersFileAtAllFails)
{
    int ok = -1;
    RunWith(Lines({ "admin", "admin123", "admin", "admin123", "admin", "admin123" }),
            [&] { ok = auth_login(); });
    EXPECT_EQ(0, ok);
}

TEST_F(AuthTest, Login_StaffUserHasNoDefaultPasswordWarning)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    std::string out;
    EXPECT_EQ(1, Login("staff1", "staffpw1", &out));
    EXPECT_EQ(ROLE_STAFF, g_current_user.role);
    EXPECT_HAS(out, "Welcome, staff1 (STAFF).");
    EXPECT_LACKS(out, "Warning");
}

TEST_F(AuthTest, Login_AdminWithChangedPasswordHasNoWarning)
{
    AddUserViaMenu("boss1", "bosspw1", ROLE_ADMIN);
    std::string out;
    EXPECT_EQ(1, Login("boss1", "bosspw1", &out));
    EXPECT_EQ(ROLE_ADMIN, g_current_user.role);
    EXPECT_LACKS(out, "Warning");
}

/*=====================================================
  change_own_password()
=====================================================*/
TEST_F(AuthTest, ChangePassword_SuccessAndNewPasswordWorks)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "admin123", "newpass1", "newpass1" }),
                              [] { change_own_password(); });
    EXPECT_HAS(out, "Password changed.");

    std::string loginOut;
    EXPECT_EQ(1, Login("admin", "newpass1", &loginOut));
    EXPECT_LACKS(loginOut, "Warning");
}

TEST_F(AuthTest, ChangePassword_OldPasswordStopsWorking)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "admin123", "newpass1", "newpass1" }), [] { change_own_password(); });

    int ok = -1;
    RunWith(Lines({ "admin", "admin123", "admin", "admin123", "admin", "admin123" }),
            [&] { ok = auth_login(); });
    EXPECT_EQ(0, ok);
}

TEST_F(AuthTest, ChangePassword_RefreshesCurrentUserHash)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "admin123", "newpass1", "newpass1" }), [] { change_own_password(); });
    std::vector<User> u = LoadUsers();
    ASSERT_EQ(1u, u.size());
    EXPECT_STREQ(u[0].hash, g_current_user.hash);
}

TEST_F(AuthTest, ChangePassword_WrongCurrentPasswordRejected)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "nope" }), [] { change_own_password(); });
    EXPECT_HAS(out, "Current password is incorrect.");
    EXPECT_EQ(1, Login("admin", "admin123"));
}

TEST_F(AuthTest, ChangePassword_TooShortRejected)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "admin123", "abc" }), [] { change_own_password(); });
    EXPECT_HAS(out, "Password must be at least 6 characters.");
    EXPECT_EQ(1, Login("admin", "admin123"));
}

TEST_F(AuthTest, ChangePassword_TooLongRejected)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "admin123", std::string(41, 'a') }),
                              [] { change_own_password(); });
    EXPECT_HAS(out, "Password is too long (max 40).");
}

TEST_F(AuthTest, ChangePassword_ConfirmationMismatchRejected)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "admin123", "newpass1", "different" }),
                              [] { change_own_password(); });
    EXPECT_HAS(out, "Passwords do not match.");
    EXPECT_EQ(1, Login("admin", "admin123"));
}

TEST_F(AuthTest, ChangePassword_MinimumLengthBoundaryAccepted)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "admin123", "abcdef", "abcdef" }),
                              [] { change_own_password(); });
    EXPECT_HAS(out, "Password changed.");
}

TEST_F(AuthTest, ChangePassword_MaximumLengthBoundaryAccepted)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string pw(40, 'p');
    std::string out = RunWith(Lines({ "admin123", pw, pw }), [] { change_own_password(); });
    EXPECT_HAS(out, "Password changed.");
    EXPECT_EQ(1, Login("admin", pw.c_str()));
}

TEST_F(AuthTest, ChangePassword_AccountNoLongerExists)
{
    InitAuth();
    SetCurrentUser("ghost", ROLE_STAFF);
    std::string out = RunWith("", [] { change_own_password(); });
    EXPECT_HAS(out, "Your account no longer exists.");
}

/*=====================================================
  user_management_menu()  (list / add / delete / reset)
=====================================================*/
TEST_F(AuthTest, UserMenu_StaffIsDenied)
{
    InitAuth();
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { user_management_menu(); });
    EXPECT_HAS(out, "Access denied: ADMIN privileges required.");
    EXPECT_LACKS(out, "USER MANAGEMENT");
}

TEST_F(AuthTest, UserMenu_InvalidChoiceReprompts)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "9", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "USER MANAGEMENT");
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(AuthTest, UserMenu_ListUsers)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "Total users: 2");
    EXPECT_HAS(out, "admin");
    EXPECT_HAS(out, "staff1");
    EXPECT_HAS(out, "STAFF");
}

TEST_F(AuthTest, UserMenu_AddStaffUser)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "clerk_1", "secret1", "secret1", "1", "0" }),
                              [] { user_management_menu(); });
    EXPECT_HAS(out, "User 'clerk_1' created as STAFF.");
    std::vector<User> u = LoadUsers();
    ASSERT_EQ(2u, u.size());
    EXPECT_STREQ("clerk_1", u[1].username);
    EXPECT_EQ(ROLE_STAFF, u[1].role);
}

TEST_F(AuthTest, UserMenu_AddAdminUser)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "boss2", "secret1", "secret1", "2", "0" }),
                              [] { user_management_menu(); });
    EXPECT_HAS(out, "created as ADMIN");
    EXPECT_EQ(ROLE_ADMIN, LoadUsers()[1].role);
}

TEST_F(AuthTest, UserMenu_AddUserInvalidRoleChoiceReprompts)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "u_admin", "secret1", "secret1", "9", "2", "0" }),
                              [] { user_management_menu(); });
    EXPECT_HAS(out, "Invalid input");
    EXPECT_EQ(ROLE_ADMIN, LoadUsers()[1].role);
}

TEST_F(AuthTest, UserMenu_AddUserRejectsInvalidUsernames)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    const std::string bad[] = { "ab", std::string(21, 'x'), "bad name", "bad-name", "semi;colon" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        std::string out = RunWith(Lines({ "2", bad[i], "0" }), [] { user_management_menu(); });
        EXPECT_HAS(out, "Invalid username.");
    }
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_AddUserAcceptsBoundaryUsernames)
{
    AddUserViaMenu("abc", "secret1", ROLE_STAFF);                       /* 3 chars  */
    AddUserViaMenu(std::string(20, 'x').c_str(), "secret1", ROLE_STAFF);/* 20 chars */
    AddUserViaMenu("a_1", "secret1", ROLE_STAFF);                       /* underscore + digit */
    EXPECT_EQ(4u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_AddUserDuplicateRejectedCaseInsensitive)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "ADMIN", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "Username already exists.");
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_AddUserShortPasswordRejected)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "newuser", "abc", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "at least 6 characters");
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_AddUserPasswordMismatchRejected)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "newuser", "abcdef", "abcdeg", "0" }),
                              [] { user_management_menu(); });
    EXPECT_HAS(out, "Passwords do not match.");
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_AddUserPasswordTooLongRejected)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "newuser", std::string(41, 'a'), "0" }),
                              [] { user_management_menu(); });
    EXPECT_HAS(out, "too long");
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_DeleteUserNotFound)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "3", "ghost", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "User not found.");
}

TEST_F(AuthTest, UserMenu_CannotDeleteOwnAccount)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "3", "ADMIN", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "You cannot delete your own account.");
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_DeleteUserCancelled)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "3", "staff1", "n", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "Cancelled.");
    EXPECT_EQ(2u, LoadUsers().size());
}

TEST_F(AuthTest, UserMenu_DeleteUserConfirmed)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "3", "staff1", "y", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "User deleted.");
    std::vector<User> u = LoadUsers();
    ASSERT_EQ(1u, u.size());
    EXPECT_STREQ("admin", u[0].username);
}

TEST_F(AuthTest, UserMenu_DeleteMiddleUserKeepsOthersIntact)
{
    AddUserViaMenu("user_a", "secret1", ROLE_STAFF);
    AddUserViaMenu("user_b", "secret1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "3", "user_a", "y", "0" }), [] { user_management_menu(); });
    std::vector<User> u = LoadUsers();
    ASSERT_EQ(2u, u.size());
    EXPECT_STREQ("admin", u[0].username);
    EXPECT_STREQ("user_b", u[1].username);
}

TEST_F(AuthTest, UserMenu_ResetPasswordNotFound)
{
    InitAuth();
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "4", "ghost", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "User not found.");
}

TEST_F(AuthTest, UserMenu_ResetPasswordSuccess)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "4", "staff1", "brandnew1", "brandnew1", "0" }),
                              [] { user_management_menu(); });
    EXPECT_HAS(out, "Password reset for 'staff1'.");
    EXPECT_EQ(1, Login("staff1", "brandnew1"));
}

TEST_F(AuthTest, UserMenu_ResetPasswordInvalidatesOldPassword)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "4", "staff1", "brandnew1", "brandnew1", "0" }), [] { user_management_menu(); });
    int ok = -1;
    RunWith(Lines({ "staff1", "staffpw1", "staff1", "staffpw1", "staff1", "staffpw1" }),
            [&] { ok = auth_login(); });
    EXPECT_EQ(0, ok);
}

TEST_F(AuthTest, UserMenu_ResetPasswordMismatchRejected)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "4", "staff1", "brandnew1", "other", "0" }),
                              [] { user_management_menu(); });
    EXPECT_HAS(out, "Passwords do not match.");
    EXPECT_EQ(1, Login("staff1", "staffpw1"));
}

TEST_F(AuthTest, UserMenu_ResetPasswordTooShortRejected)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "4", "staff1", "abc", "0" }), [] { user_management_menu(); });
    EXPECT_HAS(out, "at least 6 characters");
}

/*#####################################################
  inventory.c
#####################################################*/

/*=====================================================
  print_item_header() / print_item()
=====================================================*/
TEST_F(InventoryTest, PrintItemHeader_ShowsAllColumns)
{
    std::string out = RunWith("", [] { print_item_header(); });
    EXPECT_HAS(out, "ID");
    EXPECT_HAS(out, "Name");
    EXPECT_HAS(out, "Category");
    EXPECT_HAS(out, "Qty");
    EXPECT_HAS(out, "Price");
    EXPECT_HAS(out, "Expiry");
    EXPECT_HAS(out, "MinLvl");
}

TEST_F(InventoryTest, PrintItem_ShowsAllFields)
{
    Item it = MakeItem(7, "Paracetamol", "Medicine", 120, 12.5f, "2030-05-06", 15);
    std::string out = RunWith("", [&] { print_item(&it); });
    EXPECT_HAS(out, "7");
    EXPECT_HAS(out, "Paracetamol");
    EXPECT_HAS(out, "Medicine");
    EXPECT_HAS(out, "120");
    EXPECT_HAS(out, "12.50");
    EXPECT_HAS(out, "2030-05-06");
    EXPECT_HAS(out, "15");
}

TEST_F(InventoryTest, PrintItem_TruncatesLongName)
{
    Item it = MakeItem(1, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmn", "C", 1, 1.0f, "2030-01-01", 1);
    std::string out = RunWith("", [&] { print_item(&it); });
    EXPECT_HAS(out, "ABCDEFGHIJKLMNOPQRSTUVWX");
    EXPECT_LACKS(out, "ABCDEFGHIJKLMNOPQRSTUVWXY");
}

/*=====================================================
  find_item_index()
=====================================================*/
TEST_F(InventoryTest, FindItemIndex_MiddleItem)
{
    Item items[3] = { MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1),
                      MakeItem(2, "B", "x", 1, 1, "2030-01-01", 1),
                      MakeItem(3, "C", "x", 1, 1, "2030-01-01", 1) };
    EXPECT_EQ(1, find_item_index(items, 3, 2));
}

TEST_F(InventoryTest, FindItemIndex_FirstItem)
{
    Item items[2] = { MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1),
                      MakeItem(2, "B", "x", 1, 1, "2030-01-01", 1) };
    EXPECT_EQ(0, find_item_index(items, 2, 1));
}

TEST_F(InventoryTest, FindItemIndex_LastItem)
{
    Item items[2] = { MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1),
                      MakeItem(2, "B", "x", 1, 1, "2030-01-01", 1) };
    EXPECT_EQ(1, find_item_index(items, 2, 2));
}

TEST_F(InventoryTest, FindItemIndex_NotFound)
{
    Item items[2] = { MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1),
                      MakeItem(2, "B", "x", 1, 1, "2030-01-01", 1) };
    EXPECT_EQ(-1, find_item_index(items, 2, 99));
}

TEST_F(InventoryTest, FindItemIndex_EmptyArray)
{
    EXPECT_EQ(-1, find_item_index(NULL, 0, 1));
}

/*=====================================================
  add_item()
=====================================================*/
TEST_F(InventoryTest, AddItem_StaffIsDenied)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { add_item(); });
    EXPECT_HAS(out, "Access denied: ADMIN privileges required.");
    EXPECT_EQ(0u, LoadItems().size());
}

TEST_F(InventoryTest, AddItem_SavesAllFieldsWithFirstId)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string exp = DateOffset(365);
    std::string out = RunWith(Lines({ "Paracetamol", "Medicine", "100", "12.50", exp, "10" }),
                              [] { add_item(); });
    EXPECT_HAS(out, "Item added with ID 1.");

    std::vector<Item> v = LoadItems();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(1, v[0].id);
    EXPECT_STREQ("Paracetamol", v[0].name);
    EXPECT_STREQ("Medicine", v[0].category);
    EXPECT_EQ(100, v[0].quantity);
    EXPECT_FLOAT_EQ(12.5f, v[0].price);
    EXPECT_EQ(exp, std::string(v[0].expiry));
    EXPECT_EQ(10, v[0].min_level);
}

TEST_F(InventoryTest, AddItem_NextIdIsMaxPlusOne)
{
    SeedItems({ MakeItem(5, "A", "x", 1, 1, "2030-01-01", 1),
                MakeItem(9, "B", "x", 1, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "New", "Cat", "1", "1", "2030-01-01", "1" }), [] { add_item(); });
    EXPECT_HAS(out, "Item added with ID 10.");
    EXPECT_EQ(3u, LoadItems().size());
}

TEST_F(InventoryTest, AddItem_EmptyNameIsReprompted)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "", "", "Widget", "Cat", "1", "1", "2030-01-01", "1" }), [] { add_item(); });
    std::vector<Item> v = LoadItems();
    ASSERT_EQ(1u, v.size());
    EXPECT_STREQ("Widget", v[0].name);
}

TEST_F(InventoryTest, AddItem_InvalidDateIsReprompted)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "W", "C", "1", "1", "bad-date", "2027-13-01", "2031-02-03", "1" }),
                              [] { add_item(); });
    EXPECT_HAS(out, "Invalid date format.");
    EXPECT_STREQ("2031-02-03", LoadItems()[0].expiry);
}

TEST_F(InventoryTest, AddItem_InvalidNumbersAreReprompted)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "W", "C", "-5", "abc", "7", "-1.5", "x", "2.25", "2030-01-01", "-1", "3" }),
                              [] { add_item(); });
    EXPECT_HAS(out, "Invalid input");
    std::vector<Item> v = LoadItems();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(7, v[0].quantity);
    EXPECT_FLOAT_EQ(2.25f, v[0].price);
    EXPECT_EQ(3, v[0].min_level);
}

TEST_F(InventoryTest, AddItem_ZeroQuantityAndPriceAllowed)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "W", "C", "0", "0", "2030-01-01", "0" }), [] { add_item(); });
    std::vector<Item> v = LoadItems();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(0, v[0].quantity);
    EXPECT_FLOAT_EQ(0.0f, v[0].price);
}

/*=====================================================
  view_items()
=====================================================*/
TEST_F(InventoryTest, ViewItems_EmptyInventory)
{
    std::string out = RunWith("", [] { view_items(); });
    EXPECT_HAS(out, "Inventory is empty.");
}

TEST_F(InventoryTest, ViewItems_ListsAllItemsWithTotal)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1),
                MakeItem(2, "Syringe", "Equipment", 20, 1, "2030-01-01", 1) });
    std::string out = RunWith("", [] { view_items(); });
    EXPECT_HAS(out, "Paracetamol");
    EXPECT_HAS(out, "Syringe");
    EXPECT_HAS(out, "Total items: 2");
}

/*=====================================================
  search_item()
=====================================================*/
TEST_F(InventoryTest, SearchItem_EmptyInventory)
{
    std::string out = RunWith("", [] { search_item(); });
    EXPECT_HAS(out, "Inventory is empty.");
}

TEST_F(InventoryTest, SearchItem_ByIdFound)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1),
                MakeItem(2, "Syringe", "Equipment", 20, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "1", "2" }), [] { search_item(); });
    EXPECT_HAS(out, "Syringe");
    EXPECT_LACKS(out, "Paracetamol");
}

TEST_F(InventoryTest, SearchItem_ByIdNotFound)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "1", "99" }), [] { search_item(); });
    EXPECT_HAS(out, "No matching item found.");
}

TEST_F(InventoryTest, SearchItem_ByNameKeywordCaseInsensitive)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1),
                MakeItem(2, "Syringe", "Equipment", 20, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "2", "PARA" }), [] { search_item(); });
    EXPECT_HAS(out, "Paracetamol");
    EXPECT_LACKS(out, "Syringe");
}

TEST_F(InventoryTest, SearchItem_ByCategoryKeyword)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1),
                MakeItem(2, "Syringe", "Equipment", 20, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "2", "equip" }), [] { search_item(); });
    EXPECT_HAS(out, "Syringe");
    EXPECT_LACKS(out, "Paracetamol");
}

TEST_F(InventoryTest, SearchItem_KeywordMatchesMultipleItems)
{
    SeedItems({ MakeItem(1, "Face Mask", "Safety", 10, 1, "2030-01-01", 1),
                MakeItem(2, "Mask Strap", "Safety", 20, 1, "2030-01-01", 1),
                MakeItem(3, "Gloves", "Safety", 5, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "2", "mask" }), [] { search_item(); });
    EXPECT_HAS(out, "Face Mask");
    EXPECT_HAS(out, "Mask Strap");
    EXPECT_LACKS(out, "Gloves");
}

TEST_F(InventoryTest, SearchItem_KeywordWithNoMatch)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "2", "zzz" }), [] { search_item(); });
    EXPECT_HAS(out, "No matching item found.");
}

TEST_F(InventoryTest, SearchItem_EmptyKeywordListsEverything)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1),
                MakeItem(2, "Syringe", "Equipment", 20, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "2", "" }), [] { search_item(); });
    EXPECT_HAS(out, "Paracetamol");
    EXPECT_HAS(out, "Syringe");
}

TEST_F(InventoryTest, SearchItem_InvalidChoiceReprompts)
{
    SeedItems({ MakeItem(1, "Paracetamol", "Medicine", 10, 1, "2030-01-01", 1) });
    std::string out = RunWith(Lines({ "7", "1", "1" }), [] { search_item(); });
    EXPECT_HAS(out, "Invalid input");
    EXPECT_HAS(out, "Paracetamol");
}

/*=====================================================
  delete_item()
=====================================================*/
TEST_F(InventoryTest, DeleteItem_StaffIsDenied)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { delete_item(); });
    EXPECT_HAS(out, "Access denied");
    EXPECT_EQ(1u, LoadItems().size());
}

TEST_F(InventoryTest, DeleteItem_EmptyInventory)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith("", [] { delete_item(); });
    EXPECT_HAS(out, "Inventory is empty.");
}

TEST_F(InventoryTest, DeleteItem_NotFound)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "99" }), [] { delete_item(); });
    EXPECT_HAS(out, "Item not found.");
    EXPECT_EQ(1u, LoadItems().size());
}

TEST_F(InventoryTest, DeleteItem_BlockedByPendingRequest)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "WardA", 1, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1" }), [] { delete_item(); });
    EXPECT_HAS(out, "Cannot delete: 1 open request(s)");
    EXPECT_EQ(1u, LoadItems().size());
}

TEST_F(InventoryTest, DeleteItem_BlockedByApprovedRequest)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "WardA", 1, REQ_APPROVED), MakeReq(2, 1, "WardB", 1, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1" }), [] { delete_item(); });
    EXPECT_HAS(out, "Cannot delete: 2 open request(s)");
}

TEST_F(InventoryTest, DeleteItem_ClosedRequestsDoNotBlock)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "WardA", 1, REQ_REJECTED), MakeReq(2, 1, "WardB", 1, REQ_ISSUED) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "y" }), [] { delete_item(); });
    EXPECT_HAS(out, "Item deleted.");
    EXPECT_EQ(0u, LoadItems().size());
}

TEST_F(InventoryTest, DeleteItem_OpenRequestForOtherItemDoesNotBlock)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1), MakeItem(2, "B", "x", 1, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 2, "WardA", 1, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "y" }), [] { delete_item(); });
    EXPECT_HAS(out, "Item deleted.");
}

TEST_F(InventoryTest, DeleteItem_CancelKeepsItem)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "n" }), [] { delete_item(); });
    EXPECT_HAS(out, "Cancelled.");
    EXPECT_EQ(1u, LoadItems().size());
}

TEST_F(InventoryTest, DeleteItem_MiddleItemShiftsRemaining)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1),
                MakeItem(2, "B", "x", 1, 1, "2030-01-01", 1),
                MakeItem(3, "C", "x", 1, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "2", "y" }), [] { delete_item(); });
    std::vector<Item> v = LoadItems();
    ASSERT_EQ(2u, v.size());
    EXPECT_EQ(1, v[0].id);
    EXPECT_EQ(3, v[1].id);
}

TEST_F(InventoryTest, DeleteItem_LastItemInList)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1), MakeItem(2, "B", "x", 1, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "2", "y" }), [] { delete_item(); });
    std::vector<Item> v = LoadItems();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(1, v[0].id);
}

/*=====================================================
  inventory_menu()
=====================================================*/
TEST_F(InventoryTest, InventoryMenu_AdminShowsFullMenu)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "0" }), [] { inventory_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "===== INVENTORY =====");
    EXPECT_HAS(out, "Add Item");
    EXPECT_HAS(out, "Delete");
}

TEST_F(InventoryTest, InventoryMenu_AdminAddViaMenu)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "1", "Widget", "Cat", "5", "1.5", "2030-01-01", "2", "0" }),
            [] { inventory_menu(ROLE_ADMIN); });
    EXPECT_EQ(1u, LoadItems().size());
}

TEST_F(InventoryTest, InventoryMenu_AdminViewSearchDelete)
{
    SeedItems({ MakeItem(1, "Widget", "Cat", 5, 1.5f, "2030-01-01", 2) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "3", "1", "1", "4", "1", "y", "0" }),
                              [] { inventory_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "Total items: 1");
    EXPECT_HAS(out, "Item deleted.");
    EXPECT_EQ(0u, LoadItems().size());
}

TEST_F(InventoryTest, InventoryMenu_AdminInvalidChoiceReprompts)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "9", "0" }), [] { inventory_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(InventoryTest, InventoryMenu_StaffSeesLookupOnly)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "0" }), [] { inventory_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "===== ITEM LOOKUP =====");
    EXPECT_LACKS(out, "Add Item");
}

TEST_F(InventoryTest, InventoryMenu_StaffViewAndSearch)
{
    SeedItems({ MakeItem(1, "Widget", "Cat", 5, 1.5f, "2030-01-01", 2) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1", "2", "2", "wid", "0" }), [] { inventory_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "Total items: 1");
    EXPECT_HAS(out, "Widget");
}

TEST_F(InventoryTest, InventoryMenu_StaffCannotPickAdminOptions)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "3", "0" }), [] { inventory_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "Invalid input");
}

/*#####################################################
  stock.c
#####################################################*/

/*=====================================================
  update_stock()
=====================================================*/
TEST_F(StockTest, UpdateStock_StaffIsDenied)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { update_stock(); });
    EXPECT_HAS(out, "Access denied");
    EXPECT_EQ(10, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_EmptyInventory)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith("", [] { update_stock(); });
    EXPECT_HAS(out, "Inventory is empty. Add items first.");
}

TEST_F(StockTest, UpdateStock_ItemNotFound)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "99" }), [] { update_stock(); });
    EXPECT_HAS(out, "Item not found.");
}

TEST_F(StockTest, UpdateStock_AddStock)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "1", "5" }), [] { update_stock(); });
    EXPECT_HAS(out, "Stock updated. New quantity of 'Widget': 15");
    EXPECT_EQ(15, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_AddStockOverflowRejected)
{
    SeedItems({ MakeItem(1, "A", "x", 95000000, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "1", "10000000" }), [] { update_stock(); });
    EXPECT_HAS(out, "Quantity too large.");
    EXPECT_EQ(95000000, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_RemoveStock)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "2", "4" }), [] { update_stock(); });
    EXPECT_HAS(out, "New quantity of 'Widget': 6");
    EXPECT_EQ(6, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_RemoveExactlyAllStock)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "1", "2", "10" }), [] { update_stock(); });
    EXPECT_EQ(0, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_RemoveMoreThanAvailableRejected)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "2", "11" }), [] { update_stock(); });
    EXPECT_HAS(out, "Cannot remove more than current stock (10).");
    EXPECT_EQ(10, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_SetExactQuantity)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "1", "3", "77" }), [] { update_stock(); });
    EXPECT_EQ(77, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_SetExactQuantityToZero)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "1", "3", "0" }), [] { update_stock(); });
    EXPECT_EQ(0, LoadItems()[0].quantity);
}

TEST_F(StockTest, UpdateStock_OnlyTargetItemIsChanged)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1), MakeItem(2, "B", "x", 20, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "2", "1", "5" }), [] { update_stock(); });
    std::vector<Item> v = LoadItems();
    EXPECT_EQ(10, v[0].quantity);
    EXPECT_EQ(25, v[1].quantity);
}

/*=====================================================
  verify_stock()
=====================================================*/
TEST_F(StockTest, VerifyStock_StaffIsDenied)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { verify_stock(); });
    EXPECT_HAS(out, "Access denied");
}

TEST_F(StockTest, VerifyStock_EmptyInventory)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith("", [] { verify_stock(); });
    EXPECT_HAS(out, "Inventory is empty.");
}

TEST_F(StockTest, VerifyStock_ReportsEveryStatusAndSummary)
{
    SeedItems({ MakeItem(1, "OkItem",      "x", 100, 1, DateOffset(365), 10),
                MakeItem(2, "ExpiredItem", "x", 100, 1, DateOffset(-5),  10),
                MakeItem(3, "SoonItem",    "x", 100, 1, DateOffset(10),  10),
                MakeItem(4, "OutItem",     "x", 0,   1, DateOffset(365), 10),
                MakeItem(5, "LowItem",     "x", 5,   1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "n" }), [] { verify_stock(); });
    EXPECT_HAS(out, "STOCK VERIFICATION");
    EXPECT_HAS(out, "EXPIRED");
    EXPECT_HAS(out, "EXPIRES-SOON");
    EXPECT_HAS(out, "OUT-OF-STOCK");
    EXPECT_HAS(out, "LOW-STOCK");
    EXPECT_HAS(out, "OK");
    EXPECT_HAS(out, "Summary: 2 low/out of stock, 1 expired, 1 expiring within 30 days.");
}

TEST_F(StockTest, VerifyStock_ExpiryWindowBoundaries)
{
    SeedItems({ MakeItem(1, "Today",   "x", 100, 1, DateOffset(0),  10),
                MakeItem(2, "Day30",   "x", 100, 1, DateOffset(30), 10),
                MakeItem(3, "Day31",   "x", 100, 1, DateOffset(31), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "n" }), [] { verify_stock(); });
    EXPECT_HAS(out, "Summary: 0 low/out of stock, 0 expired, 2 expiring within 30 days.");
}

TEST_F(StockTest, VerifyStock_LowStockThresholdIsInclusive)
{
    SeedItems({ MakeItem(1, "AtMin",    "x", 10, 1, DateOffset(365), 10),
                MakeItem(2, "AboveMin", "x", 11, 1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "n" }), [] { verify_stock(); });
    EXPECT_HAS(out, "Summary: 1 low/out of stock, 0 expired, 0 expiring within 30 days.");
}

TEST_F(StockTest, VerifyStock_ExpiredAndOutOfStockCombined)
{
    SeedItems({ MakeItem(1, "Both", "x", 0, 1, DateOffset(-1), 5) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "n" }), [] { verify_stock(); });
    EXPECT_HAS(out, "EXPIRED");
    EXPECT_HAS(out, "OUT-OF-STOCK");
    EXPECT_HAS(out, "Summary: 1 low/out of stock, 1 expired, 0 expiring within 30 days.");
}

TEST_F(StockTest, VerifyStock_ReconcileMatchingCount)
{
    SeedItems({ MakeItem(1, "A", "x", 100, 1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "y", "1", "100", "0" }), [] { verify_stock(); });
    EXPECT_HAS(out, "Verified: record matches physical count (100).");
    EXPECT_LACKS(out, "Corrections saved.");
}

TEST_F(StockTest, VerifyStock_ReconcileMismatchCorrected)
{
    SeedItems({ MakeItem(1, "A", "x", 100, 1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "y", "1", "90", "y", "0" }), [] { verify_stock(); });
    EXPECT_HAS(out, "MISMATCH: record=100, counted=90 (difference -10)");
    EXPECT_HAS(out, "Record corrected.");
    EXPECT_HAS(out, "Corrections saved.");
    EXPECT_EQ(90, LoadItems()[0].quantity);
}

TEST_F(StockTest, VerifyStock_ReconcileSurplusShowsPositiveDifference)
{
    SeedItems({ MakeItem(1, "A", "x", 100, 1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "y", "1", "110", "y", "0" }), [] { verify_stock(); });
    EXPECT_HAS(out, "(difference +10)");
    EXPECT_EQ(110, LoadItems()[0].quantity);
}

TEST_F(StockTest, VerifyStock_ReconcileMismatchDeclinedLeavesRecord)
{
    SeedItems({ MakeItem(1, "A", "x", 100, 1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "y", "1", "90", "n", "0" }), [] { verify_stock(); });
    EXPECT_HAS(out, "MISMATCH");
    EXPECT_LACKS(out, "Corrections saved.");
    EXPECT_EQ(100, LoadItems()[0].quantity);
}

TEST_F(StockTest, VerifyStock_ReconcileUnknownItemId)
{
    SeedItems({ MakeItem(1, "A", "x", 100, 1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "y", "99", "0" }), [] { verify_stock(); });
    EXPECT_HAS(out, "Item not found.");
}

TEST_F(StockTest, VerifyStock_ReconcileSeveralItemsInOneSession)
{
    SeedItems({ MakeItem(1, "A", "x", 100, 1, DateOffset(365), 10),
                MakeItem(2, "B", "x", 50,  1, DateOffset(365), 10) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "y", "1", "99", "y", "2", "40", "y", "0" }), [] { verify_stock(); });
    std::vector<Item> v = LoadItems();
    EXPECT_EQ(99, v[0].quantity);
    EXPECT_EQ(40, v[1].quantity);
}

/*=====================================================
  stock_menu()
=====================================================*/
TEST_F(StockTest, StockMenu_ShowsMenuAndExits)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "0" }), [] { stock_menu(); });
    EXPECT_HAS(out, "===== STOCK =====");
    EXPECT_HAS(out, "Update");
    EXPECT_HAS(out, "Verify");
}

TEST_F(StockTest, StockMenu_UpdateOption)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "1", "1", "3", "5", "0" }), [] { stock_menu(); });
    EXPECT_EQ(5, LoadItems()[0].quantity);
}

TEST_F(StockTest, StockMenu_VerifyOption)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "n", "0" }), [] { stock_menu(); });
    EXPECT_HAS(out, "STOCK VERIFICATION");
}

TEST_F(StockTest, StockMenu_StaffActionsAreDenied)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1", "2", "0" }), [] { stock_menu(); });
    EXPECT_HAS(out, "Access denied");
}

TEST_F(StockTest, StockMenu_InvalidChoiceReprompts)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "5", "0" }), [] { stock_menu(); });
    EXPECT_HAS(out, "Invalid input");
}

/*#####################################################
  requests.c
#####################################################*/

/*=====================================================
  status_name()
=====================================================*/
TEST_F(RequestsTest, StatusName_Pending)   { EXPECT_STREQ("PENDING",  status_name(REQ_PENDING)); }
TEST_F(RequestsTest, StatusName_Approved)  { EXPECT_STREQ("APPROVED", status_name(REQ_APPROVED)); }
TEST_F(RequestsTest, StatusName_Rejected)  { EXPECT_STREQ("REJECTED", status_name(REQ_REJECTED)); }
TEST_F(RequestsTest, StatusName_Issued)    { EXPECT_STREQ("ISSUED",   status_name(REQ_ISSUED)); }
TEST_F(RequestsTest, StatusName_Invalid)   { EXPECT_STREQ("UNKNOWN",  status_name(999)); }
TEST_F(RequestsTest, StatusName_Negative)  { EXPECT_STREQ("UNKNOWN",  status_name(-1)); }

/*=====================================================
  reserved_qty()
=====================================================*/
TEST_F(RequestsTest, ReservedQty_CountsOnlyApprovedForItem)
{
    Request r[2] = { MakeReq(1, 101, "WardA", 20, REQ_APPROVED),
                     MakeReq(2, 101, "WardA", 10, REQ_PENDING) };
    EXPECT_EQ(20, reserved_qty(r, 2, 101));
}

TEST_F(RequestsTest, ReservedQty_SumsMultipleApproved)
{
    Request r[4] = { MakeReq(1, 101, "A", 20, REQ_APPROVED),
                     MakeReq(2, 101, "B", 30, REQ_APPROVED),
                     MakeReq(3, 101, "C", 10, REQ_PENDING),
                     MakeReq(4, 102, "D", 15, REQ_APPROVED) };
    EXPECT_EQ(50, reserved_qty(r, 4, 101));
}

TEST_F(RequestsTest, ReservedQty_ItemNotPresent)
{
    Request r[2] = { MakeReq(1, 101, "A", 20, REQ_APPROVED), MakeReq(2, 102, "B", 30, REQ_APPROVED) };
    EXPECT_EQ(0, reserved_qty(r, 2, 999));
}

TEST_F(RequestsTest, ReservedQty_IgnoresRejectedAndIssued)
{
    Request r[2] = { MakeReq(1, 101, "A", 20, REQ_REJECTED), MakeReq(2, 101, "B", 30, REQ_ISSUED) };
    EXPECT_EQ(0, reserved_qty(r, 2, 101));
}

TEST_F(RequestsTest, ReservedQty_EmptyList)
{
    EXPECT_EQ(0, reserved_qty(NULL, 0, 1));
}

/*=====================================================
  create_request()
=====================================================*/
TEST_F(RequestsTest, CreateRequest_AdminIsDenied)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith("", [] { create_request(); });
    EXPECT_HAS(out, "Access denied: STAFF privileges required.");
    EXPECT_EQ(0u, LoadRequests().size());
}

TEST_F(RequestsTest, CreateRequest_EmptyInventory)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { create_request(); });
    EXPECT_HAS(out, "Inventory is empty.");
}

TEST_F(RequestsTest, CreateRequest_ItemNotFound)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "99" }), [] { create_request(); });
    EXPECT_HAS(out, "Item not found.");
    EXPECT_EQ(0u, LoadRequests().size());
}

TEST_F(RequestsTest, CreateRequest_SavesPendingRequestWithAllFields)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1", "Ward A", "5" }), [] { create_request(); });
    EXPECT_HAS(out, "Item: Widget (in stock: 10)");
    EXPECT_HAS(out, "Request #1 created (PENDING).");

    std::vector<Request> v = LoadRequests();
    ASSERT_EQ(1u, v.size());
    EXPECT_EQ(1, v[0].req_id);
    EXPECT_EQ(1, v[0].item_id);
    EXPECT_STREQ("Ward A", v[0].requester);
    EXPECT_EQ(5, v[0].qty);
    EXPECT_EQ(REQ_PENDING, v[0].status);
    EXPECT_EQ(DateOffset(0), std::string(v[0].date));
    EXPECT_STREQ("staff1", v[0].created_by);
}

TEST_F(RequestsTest, CreateRequest_IdIsMaxPlusOne)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(4, 1, "W", 1, REQ_REJECTED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1", "ICU", "2" }), [] { create_request(); });
    EXPECT_HAS(out, "Request #5 created");
    EXPECT_EQ(2u, LoadRequests().size());
}

TEST_F(RequestsTest, CreateRequest_EmptyRequesterIsReprompted)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    RunWith(Lines({ "1", "", "", "ICU", "2" }), [] { create_request(); });
    EXPECT_STREQ("ICU", LoadRequests()[0].requester);
}

TEST_F(RequestsTest, CreateRequest_InvalidQuantityIsReprompted)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1", "ICU", "0", "abc", "3" }), [] { create_request(); });
    EXPECT_HAS(out, "Invalid input");
    EXPECT_EQ(3, LoadRequests()[0].qty);
}

TEST_F(RequestsTest, CreateRequest_MayExceedCurrentStock)
{
    SeedItems({ MakeItem(1, "A", "x", 1, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    RunWith(Lines({ "1", "ICU", "500" }), [] { create_request(); });
    EXPECT_EQ(500, LoadRequests()[0].qty);
}

/*=====================================================
  list_requests()
=====================================================*/
TEST_F(RequestsTest, ListRequests_NoneToDisplay)
{
    std::string out = RunWith("", [] { list_requests(-1); });
    EXPECT_HAS(out, "No requests to display.");
}

TEST_F(RequestsTest, ListRequests_AllShowsHeaderAndRows)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "WardA", 5, REQ_PENDING, "", "staff1"),
                   MakeReq(2, 1, "WardB", 3, REQ_REJECTED, "No stock", "staff2") });
    std::string out = RunWith("", [] { list_requests(-1); });
    EXPECT_HAS(out, "ReqID");
    EXPECT_HAS(out, "Remarks");
    EXPECT_HAS(out, "Widget");
    EXPECT_HAS(out, "WardA");
    EXPECT_HAS(out, "WardB");
    EXPECT_HAS(out, "PENDING");
    EXPECT_HAS(out, "REJECTED");
    EXPECT_HAS(out, "No stock");
    EXPECT_HAS(out, "staff2");
}

TEST_F(RequestsTest, ListRequests_StatusFilterShowsOnlyMatching)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "PendingGuy", 5, REQ_PENDING),
                   MakeReq(2, 1, "ApprovedGal", 3, REQ_APPROVED) });
    std::string out = RunWith("", [] { list_requests(REQ_APPROVED); });
    EXPECT_HAS(out, "ApprovedGal");
    EXPECT_LACKS(out, "PendingGuy");
}

TEST_F(RequestsTest, ListRequests_FilterWithNoMatchShowsNone)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "WardA", 5, REQ_PENDING) });
    std::string out = RunWith("", [] { list_requests(REQ_ISSUED); });
    EXPECT_HAS(out, "No requests to display.");
}

TEST_F(RequestsTest, ListRequests_DeletedItemIsLabelled)
{
    SeedItems({ MakeItem(2, "Other", "x", 10, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 99, "WardA", 5, REQ_PENDING) });
    std::string out = RunWith("", [] { list_requests(-1); });
    EXPECT_HAS(out, "(deleted item)");
}

/*=====================================================
  approve_request()
=====================================================*/
TEST_F(RequestsTest, ApproveRequest_StaffIsDenied)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { approve_request(); });
    EXPECT_HAS(out, "Access denied: ADMIN privileges required.");
}

TEST_F(RequestsTest, ApproveRequest_NoRequestsAtAll)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith("", [] { approve_request(); });
    EXPECT_HAS(out, "No requests to display.");
}

TEST_F(RequestsTest, ApproveRequest_CancelWithZero)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "0" }), [] { approve_request(); });
    EXPECT_EQ(REQ_PENDING, LoadRequests()[0].status);
}

TEST_F(RequestsTest, ApproveRequest_SuccessSetsStatusAndRemark)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1" }), [] { approve_request(); });
    EXPECT_HAS(out, "Request #1 approved.");
    Request r = LoadRequests()[0];
    EXPECT_EQ(REQ_APPROVED, r.status);
    EXPECT_STREQ("Approved", r.remarks);
}

TEST_F(RequestsTest, ApproveRequest_UnknownIdRejected)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "42" }), [] { approve_request(); });
    EXPECT_HAS(out, "No pending request with that ID.");
}

TEST_F(RequestsTest, ApproveRequest_NonPendingIdRejected)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING), MakeReq(2, 1, "X", 5, REQ_REJECTED) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2" }), [] { approve_request(); });
    EXPECT_HAS(out, "No pending request with that ID.");
    EXPECT_EQ(REQ_REJECTED, LoadRequests()[1].status);
}

TEST_F(RequestsTest, ApproveRequest_ItemNoLongerExists)
{
    SeedItems({ MakeItem(2, "Other", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 99, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1" }), [] { approve_request(); });
    EXPECT_HAS(out, "Item no longer exists. Reject this request instead.");
    EXPECT_EQ(REQ_PENDING, LoadRequests()[0].status);
}

TEST_F(RequestsTest, ApproveRequest_ExpiredItemRejected)
{
    SeedItems({ MakeItem(1, "OldStuff", "x", 10, 1, DateOffset(-2), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1" }), [] { approve_request(); });
    EXPECT_HAS(out, "Cannot approve: 'OldStuff' is expired");
    EXPECT_EQ(REQ_PENDING, LoadRequests()[0].status);
}

TEST_F(RequestsTest, ApproveRequest_InsufficientStockRejected)
{
    SeedItems({ MakeItem(1, "A", "x", 3, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1" }), [] { approve_request(); });
    EXPECT_HAS(out, "requested 5 but only 3 available");
    EXPECT_EQ(REQ_PENDING, LoadRequests()[0].status);
}

TEST_F(RequestsTest, ApproveRequest_ReservedStockIsSubtracted)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W1", 6, REQ_APPROVED), MakeReq(2, 1, "W2", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2" }), [] { approve_request(); });
    EXPECT_HAS(out, "requested 5 but only 4 available");
    EXPECT_EQ(REQ_PENDING, LoadRequests()[1].status);
}

TEST_F(RequestsTest, ApproveRequest_ExactlyAvailableQuantityApproved)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W1", 6, REQ_APPROVED), MakeReq(2, 1, "W2", 4, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2" }), [] { approve_request(); });
    EXPECT_HAS(out, "Request #2 approved.");
}

/*=====================================================
  reject_request()
=====================================================*/
TEST_F(RequestsTest, RejectRequest_StaffIsDenied)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { reject_request(); });
    EXPECT_HAS(out, "Access denied");
}

TEST_F(RequestsTest, RejectRequest_NoRequestsAtAll)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith("", [] { reject_request(); });
    EXPECT_HAS(out, "No requests to display.");
}

TEST_F(RequestsTest, RejectRequest_CancelWithZero)
{
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "0" }), [] { reject_request(); });
    EXPECT_EQ(REQ_PENDING, LoadRequests()[0].status);
}

TEST_F(RequestsTest, RejectRequest_SuccessStoresReason)
{
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "Out of budget" }), [] { reject_request(); });
    EXPECT_HAS(out, "Request #1 rejected.");
    Request r = LoadRequests()[0];
    EXPECT_EQ(REQ_REJECTED, r.status);
    EXPECT_STREQ("Out of budget", r.remarks);
}

TEST_F(RequestsTest, RejectRequest_BlankReasonDefaultsToRejected)
{
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "1", "" }), [] { reject_request(); });
    EXPECT_STREQ("Rejected", LoadRequests()[0].remarks);
}

TEST_F(RequestsTest, RejectRequest_UnknownIdRejected)
{
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "42" }), [] { reject_request(); });
    EXPECT_HAS(out, "No pending request with that ID.");
}

TEST_F(RequestsTest, RejectRequest_AlreadyApprovedCannotBeRejected)
{
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING), MakeReq(2, 1, "X", 5, REQ_APPROVED) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2" }), [] { reject_request(); });
    EXPECT_HAS(out, "No pending request with that ID.");
    EXPECT_EQ(REQ_APPROVED, LoadRequests()[1].status);
}

/*=====================================================
  requests_menu()
=====================================================*/
TEST_F(RequestsTest, RequestsMenu_AdminShowsReviewMenu)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "0" }), [] { requests_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "REQUESTS (REVIEW)");
    EXPECT_HAS(out, "Approve");
    EXPECT_HAS(out, "Reject");
}

TEST_F(RequestsTest, RequestsMenu_AdminViewAll)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "WardA", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "0" }), [] { requests_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "WardA");
}

TEST_F(RequestsTest, RequestsMenu_AdminApprove)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "2", "1", "0" }), [] { requests_menu(ROLE_ADMIN); });
    EXPECT_EQ(REQ_APPROVED, LoadRequests()[0].status);
}

TEST_F(RequestsTest, RequestsMenu_AdminReject)
{
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("admin", ROLE_ADMIN);
    RunWith(Lines({ "3", "1", "No", "0" }), [] { requests_menu(ROLE_ADMIN); });
    EXPECT_EQ(REQ_REJECTED, LoadRequests()[0].status);
}

TEST_F(RequestsTest, RequestsMenu_StaffShowsNewAndStatus)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "0" }), [] { requests_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "===== REQUESTS =====");
    EXPECT_HAS(out, "New Request");
    EXPECT_HAS(out, "View Request Status");
}

TEST_F(RequestsTest, RequestsMenu_StaffNewRequest)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, "2030-01-01", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    RunWith(Lines({ "1", "1", "ICU", "2", "0" }), [] { requests_menu(ROLE_STAFF); });
    EXPECT_EQ(1u, LoadRequests().size());
}

TEST_F(RequestsTest, RequestsMenu_StaffViewStatus)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, "2030-01-01", 1) });
    SeedRequests({ MakeReq(1, 1, "WardA", 5, REQ_PENDING) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "2", "0" }), [] { requests_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "WardA");
}

TEST_F(RequestsTest, RequestsMenu_StaffCannotPickApprove)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "3", "0" }), [] { requests_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "Invalid input");
}

/*#####################################################
  distribution.c
#####################################################*/

/*=====================================================
  issue_supplies()
=====================================================*/
TEST_F(DistributionTest, IssueSupplies_AdminIsDenied)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith("", [] { issue_supplies(); });
    EXPECT_HAS(out, "Access denied: STAFF privileges required.");
}

TEST_F(DistributionTest, IssueSupplies_NothingApproved)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_PENDING) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { issue_supplies(); });
    EXPECT_HAS(out, "No requests to display.");
}

TEST_F(DistributionTest, IssueSupplies_NoRequestsAtAll)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith("", [] { issue_supplies(); });
    EXPECT_HAS(out, "No requests to display.");
}

TEST_F(DistributionTest, IssueSupplies_CancelWithZero)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    RunWith(Lines({ "0" }), [] { issue_supplies(); });
    EXPECT_EQ(10, LoadItems()[0].quantity);
    EXPECT_EQ(REQ_APPROVED, LoadRequests()[0].status);
}

TEST_F(DistributionTest, IssueSupplies_SuccessUpdatesStockRequestAndHistory)
{
    SeedItems({ MakeItem(1, "Widget", "x", 50, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "Ward A", 10, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "Issued 10 x Widget to Ward A. Remaining stock: 40");

    EXPECT_EQ(40, LoadItems()[0].quantity);
    Request r = LoadRequests()[0];
    EXPECT_EQ(REQ_ISSUED, r.status);
    EXPECT_STREQ("Issued", r.remarks);

    std::vector<Distribution> h = LoadHistory();
    ASSERT_EQ(1u, h.size());
    EXPECT_EQ(1, h[0].dist_id);
    EXPECT_EQ(1, h[0].req_id);
    EXPECT_EQ(1, h[0].item_id);
    EXPECT_EQ(10, h[0].qty);
    EXPECT_STREQ("Widget", h[0].item_name);
    EXPECT_STREQ("Ward A", h[0].recipient);
    EXPECT_STREQ("staff1", h[0].issued_by);
    EXPECT_EQ(DateOffset(0), std::string(h[0].date));
}

TEST_F(DistributionTest, IssueSupplies_DistributionIdsIncrement)
{
    SeedItems({ MakeItem(1, "Widget", "x", 50, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W1", 5, REQ_APPROVED), MakeReq(2, 1, "W2", 7, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    RunWith(Lines({ "1" }), [] { issue_supplies(); });
    RunWith(Lines({ "2" }), [] { issue_supplies(); });

    std::vector<Distribution> h = LoadHistory();
    ASSERT_EQ(2u, h.size());
    EXPECT_EQ(1, h[0].dist_id);
    EXPECT_EQ(2, h[1].dist_id);
    EXPECT_EQ(38, LoadItems()[0].quantity);
}

TEST_F(DistributionTest, IssueSupplies_NextIdFollowsExistingHistory)
{
    SeedItems({ MakeItem(1, "Widget", "x", 50, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W1", 5, REQ_APPROVED) });
    SeedHistory({ MakeDist(41, 99, 1, "Widget", "Old", 1) });
    SetCurrentUser("staff1", ROLE_STAFF);
    RunWith(Lines({ "1" }), [] { issue_supplies(); });
    std::vector<Distribution> h = LoadHistory();
    ASSERT_EQ(2u, h.size());
    EXPECT_EQ(42, h[1].dist_id);
}

TEST_F(DistributionTest, IssueSupplies_IssuingAllRemainingStockAllowed)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W1", 10, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "Remaining stock: 0");
    EXPECT_EQ(0, LoadItems()[0].quantity);
}

TEST_F(DistributionTest, IssueSupplies_UnknownRequestId)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "42" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "No approved request with that ID.");
}

TEST_F(DistributionTest, IssueSupplies_NonApprovedRequestIdRejected)
{
    SeedItems({ MakeItem(1, "A", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_APPROVED), MakeReq(2, 1, "X", 5, REQ_PENDING) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "2" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "No approved request with that ID.");
    EXPECT_EQ(10, LoadItems()[0].quantity);
}

TEST_F(DistributionTest, IssueSupplies_ItemNoLongerExists)
{
    SeedItems({ MakeItem(2, "Other", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 99, "W", 5, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "Item no longer exists.");
    EXPECT_EQ(0u, LoadHistory().size());
}

TEST_F(DistributionTest, IssueSupplies_ExpiredItemCannotBeIssued)
{
    SeedItems({ MakeItem(1, "OldStuff", "x", 10, 1, DateOffset(-1), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "Cannot issue: 'OldStuff' is expired");
    EXPECT_EQ(10, LoadItems()[0].quantity);
    EXPECT_EQ(REQ_APPROVED, LoadRequests()[0].status);
}

TEST_F(DistributionTest, IssueSupplies_ItemExpiringTodayCanStillBeIssued)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, DateOffset(0), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 5, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "Issued 5 x Widget");
}

TEST_F(DistributionTest, IssueSupplies_InsufficientStockBlocksIssue)
{
    SeedItems({ MakeItem(1, "A", "x", 5, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 10, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "1" }), [] { issue_supplies(); });
    EXPECT_HAS(out, "Insufficient stock: have 5, need 10.");
    EXPECT_EQ(5, LoadItems()[0].quantity);
    EXPECT_EQ(0u, LoadHistory().size());
}

/*=====================================================
  view_history()
=====================================================*/
TEST_F(DistributionTest, ViewHistory_Empty)
{
    std::string out = RunWith("", [] { view_history(); });
    EXPECT_HAS(out, "No distribution history yet.");
}

TEST_F(DistributionTest, ViewHistory_ListsRecordsAndTotals)
{
    SeedHistory({ MakeDist(1, 1, 1, "Widget", "Ward A", 10, "staff1"),
                  MakeDist(2, 2, 1, "Gauze",  "Ward B", 5,  "staff2") });
    std::string out = RunWith("", [] { view_history(); });
    EXPECT_HAS(out, "DistID");
    EXPECT_HAS(out, "Widget");
    EXPECT_HAS(out, "Ward A");
    EXPECT_HAS(out, "Gauze");
    EXPECT_HAS(out, "staff2");
    EXPECT_HAS(out, "2 distributions, 15 units issued in total.");
}

TEST_F(DistributionTest, ViewHistory_SingleRecord)
{
    SeedHistory({ MakeDist(1, 1, 1, "Widget", "Ward A", 3) });
    std::string out = RunWith("", [] { view_history(); });
    EXPECT_HAS(out, "1 distributions, 3 units issued in total.");
}

/*=====================================================
  distribution_menu()
=====================================================*/
TEST_F(DistributionTest, DistributionMenu_AdminSeesReportsOnly)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "0" }), [] { distribution_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "DISTRIBUTION (REPORTS)");
    EXPECT_LACKS(out, "Issue");
}

TEST_F(DistributionTest, DistributionMenu_AdminHistory)
{
    SeedHistory({ MakeDist(1, 1, 1, "Widget", "Ward A", 3) });
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "1", "0" }), [] { distribution_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "Widget");
}

TEST_F(DistributionTest, DistributionMenu_AdminCannotPickIssue)
{
    SetCurrentUser("admin", ROLE_ADMIN);
    std::string out = RunWith(Lines({ "2", "0" }), [] { distribution_menu(ROLE_ADMIN); });
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(DistributionTest, DistributionMenu_StaffShowsIssueAndHistory)
{
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "0" }), [] { distribution_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "===== DISTRIBUTION =====");
    EXPECT_HAS(out, "Issue");
    EXPECT_HAS(out, "History");
}

TEST_F(DistributionTest, DistributionMenu_StaffIssue)
{
    SeedItems({ MakeItem(1, "Widget", "x", 10, 1, DateOffset(365), 1) });
    SeedRequests({ MakeReq(1, 1, "W", 4, REQ_APPROVED) });
    SetCurrentUser("staff1", ROLE_STAFF);
    RunWith(Lines({ "1", "1", "0" }), [] { distribution_menu(ROLE_STAFF); });
    EXPECT_EQ(6, LoadItems()[0].quantity);
}

TEST_F(DistributionTest, DistributionMenu_StaffHistory)
{
    SeedHistory({ MakeDist(1, 1, 1, "Widget", "Ward A", 3) });
    SetCurrentUser("staff1", ROLE_STAFF);
    std::string out = RunWith(Lines({ "2", "0" }), [] { distribution_menu(ROLE_STAFF); });
    EXPECT_HAS(out, "Ward A");
}

/*#####################################################
  End-of-input behaviour  (functions that call exit(0))
#####################################################*/
static void ReadLineAtEof()  { SetInput(""); char b[8]; read_line("p> ", b, sizeof b); }
static void ReadIntAtEof()   { SetInput(""); read_int("n> ", 0, 5); }
static void ReadFloatAtEof() { SetInput(""); read_float("f> ", 0.0f, 5.0f); }
static void ConfirmAtEof()   { SetInput(""); confirm("y/n> "); }
static void PasswordAtEof()  { SetInput(""); char b[8]; read_password("pw> ", b, sizeof b); }
static void LoginAtEof()     { SetInput(""); auth_login(); }
static void MenuAtEof()      { SetInput(""); SetCurrentUser("admin", ROLE_ADMIN); user_management_menu(); }

TEST_F(InputEofDeathTest, ReadLine_ExitsCleanlyOnEof)  { EXPECT_EXIT(ReadLineAtEof(),  ::testing::ExitedWithCode(0), ""); }
TEST_F(InputEofDeathTest, ReadInt_ExitsCleanlyOnEof)   { EXPECT_EXIT(ReadIntAtEof(),   ::testing::ExitedWithCode(0), ""); }
TEST_F(InputEofDeathTest, ReadFloat_ExitsCleanlyOnEof) { EXPECT_EXIT(ReadFloatAtEof(), ::testing::ExitedWithCode(0), ""); }
TEST_F(InputEofDeathTest, Confirm_ExitsCleanlyOnEof)   { EXPECT_EXIT(ConfirmAtEof(),   ::testing::ExitedWithCode(0), ""); }
TEST_F(InputEofDeathTest, ReadPassword_ExitsCleanlyOnEof) { EXPECT_EXIT(PasswordAtEof(), ::testing::ExitedWithCode(0), ""); }
TEST_F(InputEofDeathTest, AuthLogin_ExitsCleanlyOnEof) { EXPECT_EXIT(LoginAtEof(),     ::testing::ExitedWithCode(0), ""); }
TEST_F(InputEofDeathTest, Menu_ExitsCleanlyOnEof)      { EXPECT_EXIT(MenuAtEof(),      ::testing::ExitedWithCode(0), ""); }

/*#####################################################
  main.c  (only when built with -Dmain=app_main -DCOVER_MAIN_C)
#####################################################*/
#ifdef COVER_MAIN_C

TEST_F(MainMenuTest, ExitImmediately)
{
    int rc = -1;
    std::string out = RunWith(Lines({ "0" }), [&] { rc = app_main(); });
    EXPECT_EQ(0, rc);
    EXPECT_HAS(out, "MEDICAL SUPPLY MANAGEMENT SYSTEM");
    EXPECT_HAS(out, "Goodbye.");
}

TEST_F(MainMenuTest, InvalidMainMenuChoiceReprompts)
{
    int rc = -1;
    std::string out = RunWith(Lines({ "7", "0" }), [&] { rc = app_main(); });
    EXPECT_EQ(0, rc);
    EXPECT_HAS(out, "Invalid input");
}

TEST_F(MainMenuTest, CreatesDataFilesAndDefaultAdmin)
{
    RunWith(Lines({ "0" }), [] { app_main(); });
    EXPECT_EQ(1u, LoadUsers().size());
}

TEST_F(MainMenuTest, ThreeFailedLoginsReturnOne)
{
    int rc = -1;
    std::string out = RunWith(Lines({ "1", "a", "b", "a", "b", "a", "b" }), [&] { rc = app_main(); });
    EXPECT_EQ(1, rc);
    EXPECT_HAS(out, "Too many failed attempts. Exiting.");
}

TEST_F(MainMenuTest, AdminLoginAndLogout)
{
    int rc = -1;
    std::string out = RunWith(Lines({ "1", "admin", "admin123", "0", "0" }), [&] { rc = app_main(); });
    EXPECT_EQ(0, rc);
    EXPECT_HAS(out, "ADMIN MENU   (logged in as admin)");
    EXPECT_HAS(out, "Logged out.");
    EXPECT_STREQ("", g_current_user.username);
}

TEST_F(MainMenuTest, AdminCanOpenEverySubMenu)
{
    std::string out = RunWith(
        Lines({ "1", "admin", "admin123",
                "1", "0",            /* inventory      */
                "2", "0",            /* stock          */
                "3", "0",            /* requests       */
                "4", "0",            /* distribution   */
                "5", "0",            /* user mgmt      */
                "6", "admin123", "newpass1", "newpass1",   /* change password */
                "0",                 /* logout         */
                "0" }),              /* exit           */
        [] { app_main(); });
    EXPECT_HAS(out, "===== INVENTORY =====");
    EXPECT_HAS(out, "===== STOCK =====");
    EXPECT_HAS(out, "===== REQUESTS (REVIEW) =====");
    EXPECT_HAS(out, "===== DISTRIBUTION (REPORTS) =====");
    EXPECT_HAS(out, "===== USER MANAGEMENT =====");
    EXPECT_HAS(out, "Password changed.");
}

TEST_F(MainMenuTest, StaffCanOpenEverySubMenu)
{
    AddUserViaMenu("staff1", "staffpw1", ROLE_STAFF);
    std::string out = RunWith(
        Lines({ "1", "staff1", "staffpw1",
                "1", "0",            /* items          */
                "2", "0",            /* requests       */
                "3", "0",            /* distribution   */
                "4", "staffpw1", "staffpw2", "staffpw2",   /* change password */
                "0",                 /* logout         */
                "0" }),              /* exit           */
        [] { app_main(); });
    EXPECT_HAS(out, "STAFF MENU   (logged in as staff1)");
    EXPECT_HAS(out, "===== ITEM LOOKUP =====");
    EXPECT_HAS(out, "===== REQUESTS =====");
    EXPECT_HAS(out, "===== DISTRIBUTION =====");
    EXPECT_HAS(out, "Password changed.");
}

TEST_F(MainMenuTest, TwoSessionsInARow)
{
    std::string out = RunWith(
        Lines({ "1", "admin", "admin123", "0", "1", "admin", "admin123", "0", "0" }),
        [] { app_main(); });
    size_t first = out.find("Logged out.");
    ASSERT_NE(std::string::npos, first);
    EXPECT_NE(std::string::npos, out.find("Logged out.", first + 1));
}

TEST_F(MainMenuTest, AdminMenuInvalidChoiceReprompts)
{
    std::string out = RunWith(Lines({ "1", "admin", "admin123", "9", "0", "0" }), [] { app_main(); });
    EXPECT_HAS(out, "Invalid input");
}

#endif /* COVER_MAIN_C */

/*=====================================================
  MAIN
=====================================================*/
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
