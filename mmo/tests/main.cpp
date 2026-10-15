/// @file main.cpp
/// @brief 传奇版测试入口

#define TEST_FRAMEWORK_HEADER "test_framework.h"
#include TEST_FRAMEWORK_HEADER

int main(int argc, char* argv[]) {
    std::string filter;
    if (argc > 1) filter = argv[1];

    printf("=== Legend MMO Unit Tests ===\n");
    printf("Filter: %s\n\n", filter.empty() ? "(all)" : filter.c_str());

    return test_registry().run(filter.empty() ? nullptr : filter.c_str());
}
