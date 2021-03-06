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
extern void test_PositiveInteger(void);
extern void test_NegativeInteger(void);
extern void test_PositiveFraction(void);
extern void test_NegativeFraction(void);
extern void test_IllegalNumberOfInteger(void);
extern void test_IllegalChar(void);


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
  Unity.TestFile = "hextolfp.c";
  UnityBegin("hextolfp.c");
  RUN_TEST(test_PositiveInteger, 11);
  RUN_TEST(test_NegativeInteger, 22);
  RUN_TEST(test_PositiveFraction, 33);
  RUN_TEST(test_NegativeFraction, 44);
  RUN_TEST(test_IllegalNumberOfInteger, 55);
  RUN_TEST(test_IllegalChar, 63);

  return (UnityEnd());
}
