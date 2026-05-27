#include <gtest/gtest.h>

#include <string_view>

#include "hosq/error.hh"

using namespace std::string_view_literals;

TEST(ErrorTest, InvalidStateHasStateKind) {
  const auto err = hosq::Error{.code = hosq::ErrorCode::invalid_state,
                               .offset = 0,
                               .detail = "local static detail"};

  EXPECT_EQ(err.kind(), hosq::ErrorKind::state);
  EXPECT_EQ(err.detail, "local static detail"sv);
}

TEST(ErrorTest, LengthAndLimitKinds) {
  EXPECT_EQ(hosq::kind_of(hosq::ErrorCode::depth_limit_exceeded),
            hosq::ErrorKind::limit);
  EXPECT_EQ(hosq::kind_of(hosq::ErrorCode::invalid_length),
            hosq::ErrorKind::schema);
  EXPECT_EQ(hosq::kind_of(hosq::ErrorCode::size_mismatch),
            hosq::ErrorKind::schema);
  EXPECT_EQ(hosq::kind_of(hosq::ErrorCode::validation_failed),
            hosq::ErrorKind::validation);
}
