#pragma once

auto get_current_test_name() -> const char*;
auto get_current_test_assert_count() -> int;
auto get_current_test_assert_failed_count() -> int;
auto get_current_test_elapsed() -> double;