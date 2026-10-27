#!/bin/bash

# ==============================================================================
# MEDSUPPLY CODE COVERAGE WORKFLOW
# Project: medsupply (C codebase tested with Google Test & Gcov)
# ==============================================================================

# Step 0: Clean up old object files, binaries, and previous coverage tracking files
echo "[*] Cleaning old build and coverage artifacts..."
rm -f *.o *.gcno *.gcda *.gcov test_runner coverage.html

# Step 1: Compile C source files with GCC coverage flags
# -fprofile-arcs and -ftest-coverage generate the .gcno notes needed for gcov.
# Using gcc (not g++) avoids strict C++ type-checking errors with void* pointers.
echo "[*] Compiling C source files with coverage flags..."
gcc -c -fprofile-arcs -ftest-coverage utils.c auth.c inventory.c requests.c filehandler.c stock.c distribution.c

# Step 2: Link C object files with the Google Test runner using G++
# --coverage ensures the final binary includes gcov runtime tracking functions.
# -I specifies the path to Google Test headers, and -lgtest links the framework.
echo "[*] Compiling test suite and linking binaries..."
g++ --coverage -I/usr/src/googletest/googletest/include \
    all_tests.cpp utils.o auth.o inventory.o requests.o filehandler.o stock.o distribution.o \
    -lgtest -lgtest_main -pthread -o test_runner

# Step 3: Run the test executable
# This executes all test cases and generates the runtime data (.gcda files).
echo "[*] Running test suite..."
./test_runner

# Step 4: Generate individual text-based gcov line-by-line annotation files
echo "[*] Generating terminal gcov reports..."
gcov utils.c auth.c inventory.c requests.c filehandler.c stock.c distribution.c

# Step 5: Generate the visual HTML coverage dashboard using gcovr
# This creates a color-coded, detailed web report for easy downloading/viewing.
echo "[*] Generating visual HTML report (coverage.html)..."
gcovr -r . --html --html-details -o coverage.html

echo "[+] Coverage workflow completed successfully! Open 'coverage.html' to view results."