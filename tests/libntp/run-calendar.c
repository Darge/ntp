/* AUTOGENERATED FILE. DO NOT EDIT. */

//=======Test Runner Used To Run Each Test Below=====
#define RUN_TEST(TestFunc, TestLineNum) \
{ \
  Unity.CurrentTestName = #TestFunc; \
  Unity.CurrentTestLineNumber = TestLineNum; \
  Unity.NumberOfTests++; \
  if (TEST_PROTECT()) \
  { \
      setUp(); \
      TestFunc(); \
  } \
  if (TEST_PROTECT() && !TEST_IS_IGNORED) \
  { \
    tearDown(); \
  } \
  UnityConcludeTest(); \
}

//=======Automagically Detected Files To Include=====
#include "unity.h"
#include <setjmp.h>
#include <stdio.h>

//=======External Functions This Runner Calls=====
extern void setUp(void);
extern void tearDown(void);
void resetTest(void);
extern void test_DaySplitMerge(void);
extern void test_SplitYearDays1(void);
extern void test_SplitYearDays2(void);
extern void test_RataDie1(void);
extern void test_LeapYears1(void);
extern void test_LeapYears2(void);
extern void test_RoundTripDate(void);
extern void test_RoundTripYearStart(void);
extern void test_RoundTripMonthStart(void);
extern void test_RoundTripWeekStart(void);
extern void test_RoundTripDayStart(void);


//=======Test Reset Option=====
void resetTest()
{
  tearDown();
  setUp();
}

char *progname;


//=======MAIN=====
int main(int argc, char *argv[])
{
  progname = argv[0];
  Unity.TestFile = "calendar.c";
  UnityBegin("calendar.c");
  RUN_TEST(test_DaySplitMerge, 228);
  RUN_TEST(test_SplitYearDays1, 258);
  RUN_TEST(test_SplitYearDays2, 273);
  RUN_TEST(test_RataDie1, 289);
  RUN_TEST(test_LeapYears1, 300);
  RUN_TEST(test_LeapYears2, 316);
  RUN_TEST(test_RoundTripDate, 335);
  RUN_TEST(test_RoundTripYearStart, 365);
  RUN_TEST(test_RoundTripMonthStart, 382);
  RUN_TEST(test_RoundTripWeekStart, 399);
  RUN_TEST(test_RoundTripDayStart, 416);

  return (UnityEnd());
}
