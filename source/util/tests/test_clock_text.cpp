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
    if (!ClockText::Parse(text, &time, ClockText::Separators::ColonOrDot)) {
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
  /* Finnish, only where the dot is asked for */
  TEST_ASSERT_TRUE(
      ClockText::Parse("22.18", &time, ClockText::Separators::ColonOrDot));
  TEST_ASSERT_EQ_INT(22, time.hour);
  TEST_ASSERT_EQ_INT(18, time.minute);
  TEST_ASSERT_TRUE(!time.twelve_hour);
  TEST_ASSERT_TRUE(!ClockText::Parse("22.18"));
  TEST_ASSERT_TRUE(!ClockText::Parse("9.05 PM"));
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
    if (ClockText::Parse(text) ||
        ClockText::Parse(text, nullptr, ClockText::Separators::ColonOrDot)) {
      fprintf(stderr, "    taken for a clock: '%s'\n", text);
      return 1;
    }
  }
  TEST_ASSERT_TRUE(!ClockText::Parse(nullptr));
  return 0;
}

/* The values of a game's Information screen (system software, SDK,
   platform) and other dotted numbers are no clock unless the dot is asked
   for; the colon forms need no asking. */
int test_versions_are_not_the_clock() {
  const char *const values[] = {"6.00", "10.40", "4.50", "1.00", "13.42",
                                "11.40", "01.05", "0.00", "PS5", "PS4"};
  for (const char *text : values) {
    if (ClockText::Parse(text)) {
      fprintf(stderr, "    taken for a clock: '%s'\n", text);
      return 1;
    }
  }
  ClockText::Time time;
  TEST_ASSERT_TRUE(ClockText::Parse("10:18 p. m.", &time) && time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("du.10:18", &time) && time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("PM10:18", &time) && time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("22:18", &time) && !time.twelve_hour);
  return 0;
}

/* With the dot asked for, only Finnish's own forms: the bare time, " AM",
   " PM". */
int test_dot_takes_only_finnish_forms() {
  ClockText::Time time;
  TEST_ASSERT_TRUE(
      ClockText::Parse("22.18", &time, ClockText::Separators::ColonOrDot));
  TEST_ASSERT_TRUE(!time.twelve_hour);
  TEST_ASSERT_TRUE(
      ClockText::Parse("9.05 PM", &time, ClockText::Separators::ColonOrDot));
  TEST_ASSERT_TRUE(time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("9.05\xc2\xa0" "AM", &time,
                                    ClockText::Separators::ColonOrDot));
  const char *const others[] = {"1.25 GB",    "3.50 MB",  "12.34 GB",
                                "9.05 a. m.", "9.05PM",   "9.05 PMx",
                                "9.05 AM.",   "AM 9.05",  "AM9.05",
                                "9.05 Ver",   "10.40 (PS5)"};
  for (const char *text : others) {
    if (ClockText::Parse(text, nullptr, ClockText::Separators::ColonOrDot)) {
      fprintf(stderr, "    taken for a clock: '%s'\n", text);
      return 1;
    }
  }
  return 0;
}

/* Sony's clock label with the seconds our script puts in it: taken only when
   asked for (the label is known to be the clock), with the time's own
   separator and a marker as before. */
int test_clock_label_takes_seconds() {
  const auto label = ClockText::Separators::ClockLabel;
  ClockText::Time time;
  TEST_ASSERT_TRUE(ClockText::Parse("22:18:05", &time, label));
  TEST_ASSERT_EQ_INT(22, time.hour);
  TEST_ASSERT_EQ_INT(18, time.minute);
  TEST_ASSERT_TRUE(!time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("9:05:07 PM", &time, label));
  TEST_ASSERT_TRUE(time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("PM9:05:59", &time, label));
  TEST_ASSERT_TRUE(time.twelve_hour);
  TEST_ASSERT_TRUE(ClockText::Parse("9.05.07 PM", &time, label));
  TEST_ASSERT_TRUE(ClockText::Parse("\xe2\x80\xab" "9:05:07 \xd9\x85", &time,
                                    label));
  TEST_ASSERT_TRUE(time.twelve_hour);
  /* the forms without seconds stay the clock */
  TEST_ASSERT_TRUE(ClockText::Parse("22:18", &time, label));
  TEST_ASSERT_TRUE(ClockText::Parse("10:18 p. m.", &time, label));
  const char *const others[] = {"22:18:60", "22:18:5",    "22:18.05",
                                "9.05:07 PM", "22:18:05:01", "22:18:05 9",
                                "Games",    "1.25 GB"};
  for (const char *text : others) {
    if (ClockText::Parse(text, nullptr, label)) {
      fprintf(stderr, "    taken for a clock: '%s'\n", text);
      return 1;
    }
  }
  /* elsewhere seconds are no clock */
  TEST_ASSERT_TRUE(!ClockText::Parse("22:18:05"));
  TEST_ASSERT_TRUE(
      !ClockText::Parse("22:18:05", nullptr, ClockText::Separators::ColonOrDot));
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
  failures += onion_test_run("clock_text.versions",
                             test_versions_are_not_the_clock);
  failures += onion_test_run("clock_text.finnish_dot",
                             test_dot_takes_only_finnish_forms);
  failures += onion_test_run("clock_text.clock_label_seconds",
                             test_clock_label_takes_seconds);
  return failures;
}
