#include <limits.h>
#include "unity.h"
#include "env_config.h"


TEST_CASE("ESP-IDF Version Control", "[env]")
{
    // Test if the current version is what's expected
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected_esp_version(), current_esp_version(), "ESP-IDF version mismatch!");
}