#pragma once
template <typename... Args> void test_log(Args...) {}
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
