#include <gtest/gtest.h>

#include "version.hpp"

TEST(Version, ReturnsNonEmptyString) {
    EXPECT_FALSE(liquibook::version().empty());
}

TEST(Version, MatchesExpectedM1Tag) {
    EXPECT_EQ(liquibook::version(), "0.1.0-m1");
}
