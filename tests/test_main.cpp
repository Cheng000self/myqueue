/**
 * @file test_main.cpp
 * @brief myqueue Test Suite Entry Point
 * 
 * 使用 Google Test + RapidCheck 进行单元测试和属性测试
 */

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
