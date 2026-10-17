/* Host tests for recognising the system clock's text (no PS5/Mono). */
#include "test_harness.h"

#include "clock_text.hpp"

#include <cstdio>

namespace {

/* Every 12-hour form in HomeUI's 31 language tables (msgid_time_am_var,
   msgid_time_pm_var with msgid_time_separator), 11.40 and 13.42. */
const char *const kSonyTwelveHour[] = {
    "9:05 AM",
    "9:05 PM",
    "\xe2\x80\xab" "9:05 ص", /* U+202B before the time */
    "\xe2\x80\xab" "9:05 م",
    "9:05 πμ",
    "9:05 μμ",
    "9:05 a. m.",
    "9:05 p. m.",
    "9.05 AM",
    "9.05 PM",
    "de.9:05",
    "du.9:05",
    "9:05 am",
    "9:05 pm",
    "9:05 до полудня",
    "9:05 після полудня",
    "9:05 SA",
    "9:05 CH",
    "AM9:05",
    "PM9:05",
    "10:18 після полудня",
    "12:59 p. m.",
};

int test_every_sony_twelve_hour_form() {
  for (const char *text : kSonyTwelveHour) {
    ClockText::Time time;
    if (!ClockText::Parse(text, &time)) {
      fprintf(stderr, "    not taken for a clock: '%s'\n", text);
      return 1;
    }
    TEST_ASSERT_TRUE(time.twelve_hour);
    TEST_ASSERT_TRUE(time.hour == 9 || time.hour == 10 || time.hour == 12);
  }
  return 0;
}

int test_twenty_four_hour_forms() {
  ClockText::Time time;
  TEST_ASSERT_TRUE(ClockText::Parse("22:18", &time));
  TEST_ASSERT_EQ_INT(22, time.hour);
  TEST_ASSERT_EQ_INT(18, time.minute);
  TEST_ASSERT_TRUE(!time.twelve_hour);
  /* Finnish */
  TEST_ASSERT_TRUE(ClockText::Parse("22.18", &time));
  TEST_ASSERT_TRUE(!time.twelve_hour);
  /* a direction mark is no marker */
  TEST_ASSERT_TRUE(ClockText::Parse("\xe2\x80\xab" "22:18", &time));
  TEST_ASSERT_TRUE(!time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("0:00", &time));
  return 0;
}

int test_leading_zero() {
  ClockText::Time time;
  TEST_ASSERT_TRUE(ClockText::Parse("09:05", &time));
  TEST_ASSERT_EQ_INT(2, time.hour_digits);
  TEST_ASSERT_EQ_INT(9, time.hour);
  TEST_ASSERT_TRUE(ClockText::Parse("9:05", &time));
  TEST_ASSERT_EQ_INT(1, time.hour_digits);
  return 0;
}

int test_other_labels_are_not_the_clock() {
  const char *const others[] = {
      "23.8  5:47",       "22:18:05",      "10:185",   "24:00",
      "9:60",             "9:5",           "1.5 GB",   "Ver. 2.50",
      "11.40.00",         "Duración: 1:30", "9:05 PM 9:05",
      "PM 9:05 PM",       "9:05\nPM",      "Games",    "",
      "9.05 p. m.",       "123",           "TOO LONG PREFIX 9:05",
  };
  for (const char *text : others) {
    if (ClockText::Parse(text)) {
      fprintf(stderr, "    taken for a clock: '%s'\n", text);
      return 1;
    }
  }
  TEST_ASSERT_TRUE(!ClockText::Parse(nullptr));
  return 0;
}

} // namespace

extern "C" int test_clock_text_suite(void) {
  int failures = 0;
  failures += onion_test_run("clock_text.sony_twelve_hour",
                             test_every_sony_twelve_hour_form);
  failures += onion_test_run("clock_text.twenty_four_hour",
                             test_twenty_four_hour_forms);
  failures += onion_test_run("clock_text.leading_zero", test_leading_zero);
  failures += onion_test_run("clock_text.other_labels",
                             test_other_labels_are_not_the_clock);
  return failures;
}
