// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Property.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui
{

/// How IncrementNumberRounder picks between the two neighboring multiples of its increment.
enum class RoundingAlgorithm
{
    RoundDown,             ///< toward negative infinity
    RoundUp,               ///< toward positive infinity
    RoundTowardsZero,      ///< truncate: -2.7 becomes -2
    RoundAwayFromZero,     ///< away from zero: -2.2 becomes -3
    RoundHalfDown,         ///< nearest; a tie toward negative infinity
    RoundHalfUp,           ///< nearest; a tie toward positive infinity
    RoundHalfTowardsZero,  ///< nearest; a tie toward zero
    RoundHalfAwayFromZero, ///< nearest; a tie away from zero (school rounding)
    RoundHalfToEven,       ///< nearest; a tie to the even multiple (banker's rounding): 2.5 becomes 2
    RoundHalfToOdd,        ///< nearest; a tie to the odd multiple: 2.5 becomes 3
};

/// Rounds to a multiple of `increment`, which must be positive.
///
/// Set as DecimalFormatter::rounder, it changes only the displayed text. A NumberBox keeps the
/// unrounded value.
///
/// Example:
/// @code
/// // Cash amounts in Swiss francs go to the nearest 5 cents.
/// const ui::IncrementNumberRounder cash{0.05, ui::RoundingAlgorithm::RoundHalfUp};
/// const double due = cash.round(12.33); // 12.35
///
/// // As a formatter's rounder it shapes what a NumberBox or a label shows.
/// const ui::DecimalFormatter francs{.fraction_digits = 2, .rounder = cash};
/// window.set_content(ui::Text{.text = "Due: CHF " + francs.format(12.33)}); // "Due: CHF 12.35"
///
/// // Banker's rounding sends a tie to the even neighbor, so ties don't drift a sum upward.
/// const ui::IncrementNumberRounder bankers{1.0, ui::RoundingAlgorithm::RoundHalfToEven};
/// const double whole = bankers.round(2.5) + bankers.round(3.5); // 2 + 4
/// @endcode
struct IncrementNumberRounder
{
    /// The step results are a multiple of; must be positive. 0.01 rounds to cents.
    double increment = 1.0;
    /// How ties and non-multiples are resolved; half-up by default.
    RoundingAlgorithm algorithm = RoundingAlgorithm::RoundHalfUp;

    /// `value` rounded to a neighboring multiple of `increment`, picked by `algorithm`. The
    /// result is cleaned to 15 significant digits, so 0.1-steps land on 0.3, not 0.30000000000000004.
    [[nodiscard]] double round(double value) const;
};

/// The separators and digit grouping a locale writes a number with, as a DecimalFormatter uses
/// them. The defaults are the locale-independent ones: "." for decimals, "," between groups of 3.
///
/// Example:
/// @code
/// // An invoice for a German customer, written the same whatever the user's own locale is.
/// const ui::DecimalFormatter german{
///     .fraction_digits = 2,
///     .is_grouped      = true,
///     .symbols         = ui::NumberFormatSymbols{.decimal_separator = ",", .grouping_separator = "."},
/// };
/// window.set_content(ui::Text{.text = "Summe: " + german.format(1234567.5) + " EUR"}); // "Summe: 1.234.567,50 EUR"
/// @endcode
struct NumberFormatSymbols
{
    /// The decimal separator: "." (en-US), "," (de-DE, fr-FR).
    std::string decimal_separator = ".";
    /// The grouping separator: "," (en-US), "." (de-DE), U+202F (fr-FR), U+2019 (de-CH).
    std::string grouping_separator = ",";
    /// The group sizes from the decimal separator leftward: {3} is 1,234,567, and {3, 2} is
    /// 12,34,567 (hi-IN). The last size repeats unless `repeat_last_group` is false.
    std::vector<int> group_sizes{3};
    /// Whether the last entry of `group_sizes` repeats for the remaining digits; false leaves
    /// the digits beyond the listed groups ungrouped.
    bool repeat_last_group = true;

    /// The locale-independent symbols, the default.
    [[nodiscard]] static NumberFormatSymbols invariant()
    {
        return {};
    }

    /// Member-wise comparison; set_regional_number_format() uses it to skip no-op changes.
    friend bool operator==(const NumberFormatSymbols&, const NumberFormatSymbols&) = default;
};

/// The regional format: what a DecimalFormatter without symbols of its own uses. Invariant until a
/// host sets it. Process-wide; UI thread only.
[[nodiscard]] const NumberFormatSymbols& regional_number_format();
/// Sets the regional format. The window host sets it from the user's locale. Raises
/// regional_number_format_changed() when it differs from the current one.
void set_regional_number_format(NumberFormatSymbols symbols);
/// Raised when the regional format changes: a NumberBox on the default formatter reformats. An
/// app that caches formatted text subscribes here and keeps the Subscription.
[[nodiscard]] const Signal& regional_number_format_changed();

/// Formats and parses plain decimal numbers.
///
/// A plain value handed to NumberBoxControl::set_number_formatter() or ui::NumberBox::number_formatter.
/// Unless `symbols` pins them, the separators follow the regional format at the time of each
/// call, so the same formatter writes "1,5" in de-DE and "1.5" in en-US.
///
/// Example:
/// @code
/// ui::DecimalFormatter dollars;
/// dollars.fraction_digits = 2;
/// dollars.rounder         = ui::IncrementNumberRounder{0.01, ui::RoundingAlgorithm::RoundHalfUp};
///
/// // The NumberBox shows 3 as "3.00" and reads what the user types back with the same formatter.
/// ui::Property<std::optional<double>> price{3.0};
/// window.set_content(ui::VerticalStack{
///     .spacing = 8,
///     .content = {
///         ui::NumberBox{.header = "Price", .value = price, .min = 0.0, .number_formatter = dollars},
///         ui::Text{.text = ui::bind(price, [dollars](const std::optional<double>& p) {
///             return p ? "Three for $" + dollars.format(*p * 3.0) : std::string{};
///         })},
///     },
/// });
///
/// // Text from elsewhere, a CSV cell say, parses the same way; nullopt when it is not a number.
/// const std::optional<double> cell = dollars.parse(" 4.5 "); // 4.5
/// @endcode
struct DecimalFormatter
{
    /// The fewest integer digits shown, padded with leading zeros.
    int integer_digits = 1;
    /// The fewest fraction digits shown, padded with trailing zeros. More are shown when the value
    /// has them. Set `rounder` to limit them.
    int fraction_digits = 0;
    /// Groups the integer digits with the grouping separator, by default in threes with `,`.
    bool is_grouped = false;
    /// Shows the decimal point even with no fraction digits.
    bool is_decimal_point_always_displayed = false;
    /// Rounds the value before formatting; unset formats it unrounded.
    std::optional<IncrementNumberRounder> rounder{};
    /// The separators. Unset follows regional_number_format(), the user's locale.
    /// NumberFormatSymbols::invariant() pins `.` and `,`.
    std::optional<NumberFormatSymbols> symbols{};

    /// The separators this formatter writes and reads with: `symbols`, or the regional format.
    [[nodiscard]] const NumberFormatSymbols& effective_symbols() const
    {
        return symbols ? *symbols : regional_number_format();
    }

    /// `value` as text. NaN and the infinities format as "NaN", "∞" and "-∞".
    [[nodiscard]] std::string format(double value) const;
    /// Reads the whole of `text`, surrounding spaces aside, as one number written with the decimal
    /// separator. Grouping separators are allowed when `is_grouped` is set. Returns nullopt when
    /// `text` is not a number.
    [[nodiscard]] std::optional<double> parse(std::string_view text) const;
};

/// `value` rounded to `digits` significant digits. NumberBox rounds its display to 10, which hides
/// the digits floating-point arithmetic leaves behind (0.1 + 0.2 shows as 0.3). `digits` counts
/// significant digits, not fraction digits. NaN and the infinities pass through.
[[nodiscard]] double round_significant(double value, int digits);

/// Evaluates an arithmetic expression of numbers, `+ - * / ^`, parentheses and spaces. Numbers
/// are read with `parser`, so in its locale: "1,5 + 2" is 3.5 where the decimal separator is ",".
/// `^` binds tighter than `* /`, which bind tighter than `+ -`, and each is left-associative. A
/// division by zero is NaN, and anything malformed is nullopt.
[[nodiscard]] std::optional<double> evaluate_expression(std::string_view expression, const DecimalFormatter& parser);

} // namespace ui
