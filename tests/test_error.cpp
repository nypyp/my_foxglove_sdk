/// @brief Unit tests for FoxgloveResult<T> error handling.

#include <foxglove/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("FoxgloveError - enum values match spec") {
  SECTION("None is zero") {
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::None) == 0);
  }
  SECTION("all codes defined") {
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::InvalidArgument) == 1);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::ChannelClosed) == 2);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::ServerStartFailed) == 3);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::IoError) == 4);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::SerializationError) == 5);
    REQUIRE(static_cast<int>(foxglove::FoxgloveError::ProtocolError) == 6);
  }
}

TEST_CASE("FoxgloveResult - success construction") {
  foxglove::FoxgloveResult<int> result(42);
  REQUIRE(result.has_value());
  REQUIRE(result.value() == 42);
}

TEST_CASE("FoxgloveResult - error construction") {
  foxglove::FoxgloveResult<int> result(
      tl::make_unexpected(foxglove::FoxgloveError::InvalidArgument));
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == foxglove::FoxgloveError::InvalidArgument);
}

TEST_CASE("FoxgloveResult - string type") {
  foxglove::FoxgloveResult<std::string> result(std::string("hello"));
  REQUIRE(result.has_value());
  REQUIRE(result.value() == "hello");
}

TEST_CASE("FOXGLOVE_TRY - propagates error") {
  auto failing_fn = []() -> foxglove::FoxgloveResult<int> {
    return tl::make_unexpected(foxglove::FoxgloveError::IoError);
  };

  auto caller = [&]() -> foxglove::FoxgloveResult<std::string> {
    FOXGLOVE_TRY(val, failing_fn());
    (void)val;
    return std::string("should not reach");
  };

  auto result = caller();
  REQUIRE(!result.has_value());
  REQUIRE(result.error() == foxglove::FoxgloveError::IoError);
}

TEST_CASE("FOXGLOVE_TRY - passes through on success") {
  auto success_fn = []() -> foxglove::FoxgloveResult<int> { return 42; };

  auto caller = [&]() -> foxglove::FoxgloveResult<int> {
    FOXGLOVE_TRY(val, success_fn());
    return val * 2;
  };

  auto result = caller();
  REQUIRE(result.has_value());
  REQUIRE(result.value() == 84);
}

TEST_CASE("foxglove_error_string - returns description") {
  REQUIRE(foxglove::foxglove_error_string(foxglove::FoxgloveError::None) ==
          "no error");
  REQUIRE(foxglove::foxglove_error_string(
              foxglove::FoxgloveError::InvalidArgument) ==
          "invalid argument");
}
