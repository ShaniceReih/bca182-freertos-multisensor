#include <unity.h>

#include "alarm.h"
#include "display_logic.h"
#include "system_state.h"


// ============================================================
// Unity setup / teardown
// ============================================================

void setUp(void)
{
}

void tearDown(void)
{
}


// ============================================================
// ALARM LOGIC TESTS
// Required: 5 tests
// ============================================================

void test_temperature_below_18_is_low_alarm(void)
{
    AlarmState result =
        evaluateTemperature(17.9f);

    TEST_ASSERT_EQUAL_INT(
        (int)AlarmState::LOW_TEMPERATURE,
        (int)result
    );
}


void test_temperature_exactly_18_is_normal(void)
{
    AlarmState result =
        evaluateTemperature(18.0f);

    TEST_ASSERT_EQUAL_INT(
        (int)AlarmState::NORMAL,
        (int)result
    );
}


void test_temperature_inside_normal_range_is_normal(void)
{
    AlarmState result =
        evaluateTemperature(25.0f);

    TEST_ASSERT_EQUAL_INT(
        (int)AlarmState::NORMAL,
        (int)result
    );
}


void test_temperature_exactly_30_is_normal(void)
{
    AlarmState result =
        evaluateTemperature(30.0f);

    TEST_ASSERT_EQUAL_INT(
        (int)AlarmState::NORMAL,
        (int)result
    );
}


void test_temperature_above_30_is_high_alarm(void)
{
    AlarmState result =
        evaluateTemperature(30.1f);

    TEST_ASSERT_EQUAL_INT(
        (int)AlarmState::HIGH_TEMPERATURE,
        (int)result
    );
}


// ============================================================
// DISPLAY NAVIGATION TESTS
// Required: 4 tests
// ============================================================

void test_clockwise_temperature_to_humidity(void)
{
    DisplayMode result =
        nextDisplayMode(
            DisplayMode::TEMPERATURE
        );

    TEST_ASSERT_EQUAL_INT(
        (int)DisplayMode::HUMIDITY,
        (int)result
    );
}


void test_clockwise_motion_wraps_to_temperature(void)
{
    DisplayMode result =
        nextDisplayMode(
            DisplayMode::MOTION
        );

    TEST_ASSERT_EQUAL_INT(
        (int)DisplayMode::TEMPERATURE,
        (int)result
    );
}


void test_counterclockwise_humidity_to_temperature(void)
{
    DisplayMode result =
        previousDisplayMode(
            DisplayMode::HUMIDITY
        );

    TEST_ASSERT_EQUAL_INT(
        (int)DisplayMode::TEMPERATURE,
        (int)result
    );
}


void test_counterclockwise_temperature_wraps_to_motion(void)
{
    DisplayMode result =
        previousDisplayMode(
            DisplayMode::TEMPERATURE
        );

    TEST_ASSERT_EQUAL_INT(
        (int)DisplayMode::MOTION,
        (int)result
    );
}


// ============================================================
// SYSTEM STATE TESTS
// Required: 4 tests
// ============================================================

void test_no_motion_before_timeout_remains_active(void)
{
    SystemState result =
        evaluateSystemState(
            false,
            10000UL,
            15000UL
        );

    TEST_ASSERT_EQUAL_INT(
        (int)SystemState::ACTIVE,
        (int)result
    );
}


void test_no_motion_exactly_at_timeout_becomes_inactive(void)
{
    SystemState result =
        evaluateSystemState(
            false,
            15000UL,
            15000UL
        );

    TEST_ASSERT_EQUAL_INT(
        (int)SystemState::INACTIVE,
        (int)result
    );
}


void test_no_motion_after_timeout_is_inactive(void)
{
    SystemState result =
        evaluateSystemState(
            false,
            20000UL,
            15000UL
        );

    TEST_ASSERT_EQUAL_INT(
        (int)SystemState::INACTIVE,
        (int)result
    );
}


void test_motion_detected_keeps_system_active(void)
{
    SystemState result =
        evaluateSystemState(
            true,
            20000UL,
            15000UL
        );

    TEST_ASSERT_EQUAL_INT(
        (int)SystemState::ACTIVE,
        (int)result
    );
}


// ============================================================
// Test runner
// ============================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // Alarm tests: 5
    RUN_TEST(
        test_temperature_below_18_is_low_alarm
    );

    RUN_TEST(
        test_temperature_exactly_18_is_normal
    );

    RUN_TEST(
        test_temperature_inside_normal_range_is_normal
    );

    RUN_TEST(
        test_temperature_exactly_30_is_normal
    );

    RUN_TEST(
        test_temperature_above_30_is_high_alarm
    );


    // Navigation tests: 4
    RUN_TEST(
        test_clockwise_temperature_to_humidity
    );

    RUN_TEST(
        test_clockwise_motion_wraps_to_temperature
    );

    RUN_TEST(
        test_counterclockwise_humidity_to_temperature
    );

    RUN_TEST(
        test_counterclockwise_temperature_wraps_to_motion
    );


    // System-state tests: 4
    RUN_TEST(
        test_no_motion_before_timeout_remains_active
    );

    RUN_TEST(
        test_no_motion_exactly_at_timeout_becomes_inactive
    );

    RUN_TEST(
        test_no_motion_after_timeout_is_inactive
    );

    RUN_TEST(
        test_motion_detected_keeps_system_active
    );


    return UNITY_END();
}