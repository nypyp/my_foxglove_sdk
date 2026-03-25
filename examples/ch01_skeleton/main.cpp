#include <foxglove/error.hpp>

#include <cstdio>

foxglove::FoxgloveResult<int> divide(int a, int b) {
  if (b == 0) {
    return tl::make_unexpected(foxglove::FoxgloveError::InvalidArgument);
  }
  return a / b;
}

foxglove::FoxgloveResult<int> chain_example() {
  FOXGLOVE_TRY(result, divide(10, 2));
  FOXGLOVE_TRY(result2, divide(result, 0));  // This will fail
  return result2;
}

int main() {
  auto r1 = divide(10, 2);
  if (r1.has_value()) {
    std::printf("10 / 2 = %d\n", r1.value());
  }

  auto r2 = chain_example();
  if (!r2.has_value()) {
    std::printf("Chain failed: %s\n",
                foxglove::foxglove_error_string(r2.error()).c_str());
  }

  return 0;
}
