#include <cstring>
#include <limits>

namespace dq::internal
{

constexpr std::size_t ceil_to_power_of_two(const std::size_t original,
                                           const std::size_t current_bit = 0)
{
  constexpr const std::size_t MAX_POWER =
      std::numeric_limits<std::size_t>::max() -
      (std::numeric_limits<std::size_t>::max() >> 1);

  if (original > MAX_POWER)
  {
    return MAX_POWER;
  }

  const std::size_t current_power = 1 << current_bit;

  if (current_power >= original || current_bit == MAX_POWER)
  {
    return current_power;
  }

  return ceil_to_power_of_two(original, current_bit + 1);
}

template <typename unsigned_int_t>
constexpr bool is_power_of_two(const unsigned_int_t number)
{
  return number != 0 && (number & (number - 1)) == 0;
}

template <int DIVISOR>
int mod_power_of_two(const int dividend)
{
  static_assert(is_power_of_two(DIVISOR));

  return dividend & (DIVISOR - 1);
}

}  // namespace dq::internal
