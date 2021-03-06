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
extern void test_IPv4Address(void);
extern void test_IPv6Address(void);
extern void test_SetLiVnMode1(void);
extern void test_SetLiVnMode2(void);
extern void test_PktOutput(void);
extern void test_LfpOutputBinaryFormat(void);
extern void test_LfpOutputDecimalFormat(void);


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
  Unity.TestFile = "utilities.c";
  UnityBegin("utilities.c");
  RUN_TEST(test_IPv4Address, 66);
  RUN_TEST(test_IPv6Address, 76);
  RUN_TEST(test_SetLiVnMode1, 96);
  RUN_TEST(test_SetLiVnMode2, 109);
  RUN_TEST(test_PktOutput, 124);
  RUN_TEST(test_LfpOutputBinaryFormat, 144);
  RUN_TEST(test_LfpOutputDecimalFormat, 160);

  return (UnityEnd());
}
