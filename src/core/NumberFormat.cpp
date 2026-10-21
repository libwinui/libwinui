// SPDX-License-Identifier: MIT
#include "winui/core/NumberFormat.hpp"

#include "winui/core/Property.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <stack>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <cmath>
#include <cstdlib>

namespace ui
{

namespace
{
constexpr double HalfEpsilon = 1e-9;

double round_quotient(double q, RoundingAlgorithm algorithm)
{
    const double down = std::floor(q);
    const double up   = std::ceil(q);
    switch (algorithm)
    {
        case RoundingAlgorithm::RoundDown:
            return down;
        case RoundingAlgorithm::RoundUp:
            return up;
        case RoundingAlgorithm::RoundTowardsZero:
            return std::trunc(q);
        case RoundingAlgorithm::RoundAwayFromZero:
            return q < 0.0 ? down : up;
        default:
            break;
    }
    const double fraction = q - down;
    if (std::abs(fraction - 0.5) > HalfEpsilon)
    {
        return fraction < 0.5 ? down : up;
    }
    switch (algorithm)
    {
        case RoundingAlgorithm::RoundHalfDown:
            return down;
        case RoundingAlgorithm::RoundHalfUp:
            return up;
        case RoundingAlgorithm::RoundHalfTowardsZero:
            return q < 0.0 ? up : down;
        case RoundingAlgorithm::RoundHalfAwayFromZero:
            return q < 0.0 ? down : up;
        case RoundingAlgorithm::RoundHalfToEven:
            return std::fmod(down, 2.0) == 0.0 ? down : up;
        case RoundingAlgorithm::RoundHalfToOdd:
            return std::fmod(down, 2.0) != 0.0 ? down : up;
        default:
            return up;
    }
}

std::string widen(std::string_view s)
{
    return std::string(s.begin(), s.end());
}

// Inserts `separator` into a run of ASCII digits by `sizes`, from the right: the first size is
// the group next to the decimal separator; the last repeats when `repeat`.
std::string group_digits(const std::string& digits, const NumberFormatSymbols& symbols)
{
    std::vector<std::size_t> cuts; // positions, from the left, where a separator goes
    std::size_t              end  = digits.size();
    std::size_t              next = 0;
    int                      size = 0;
    while (true)
    {
        if (next < symbols.group_sizes.size())
        {
            size = symbols.group_sizes[next++];
        }
        else if (!symbols.repeat_last_group)
        {
            break;
        }
        if (size <= 0 || end <= static_cast<std::size_t>(size))
        {
            break;
        }
        end -= static_cast<std::size_t>(size);
        cuts.push_back(end);
    }
    std::string out;
    std::size_t from = 0;
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
    {
        out += widen(std::string_view(digits).substr(from, *it - from));
        out += symbols.grouping_separator;
        from = *it;
    }
    out += widen(std::string_view(digits).substr(from));
    return out;
}

struct RegionalFormat
{
    NumberFormatSymbols symbols{};
    Signal              changed{};
};

RegionalFormat& regional()
{
    static RegionalFormat format;
    return format;
}
} // namespace

const NumberFormatSymbols& regional_number_format()
{
    return regional().symbols;
}

void set_regional_number_format(NumberFormatSymbols symbols)
{
    if (symbols == regional().symbols)
    {
        return;
    }
    regional().symbols = std::move(symbols);
    regional().changed.raise();
}

const Signal& regional_number_format_changed()
{
    return regional().changed;
}

double IncrementNumberRounder::round(double value) const
{
    if (!std::isfinite(value) || increment <= 0.0)
    {
        return value;
    }
    // Rounded to 15 significant digits first, so 0.1 * 3 lands on the multiple it means.
    const double q = round_significant(value / increment, 15);
    return round_significant(round_quotient(q, algorithm) * increment, 15);
}

double round_significant(double value, int digits)
{
    if (value == 0.0 || !std::isfinite(value))
    {
        return value;
    }
    // A double carries 17 significant digits at most, which also keeps the text inside the buffer.
    digits = std::clamp(digits, 1, 17);
    char       buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific, digits - 1);
    if (result.ec != std::errc{})
    {
        return value;
    }
    return std::strtod(std::string(buffer, result.ptr).c_str(), nullptr);
}

std::string DecimalFormatter::format(double value) const
{
    if (std::isnan(value))
    {
        return "NaN";
    }
    if (std::isinf(value))
    {
        return value < 0.0 ? "-∞" : "∞";
    }
    if (rounder)
    {
        value = rounder->round(value);
    }
    char        buffer[512];
    const auto  result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed);
    std::string text(buffer, result.ptr);

    bool negative = false;
    if (!text.empty() && text.front() == '-')
    {
        negative = true;
        text.erase(0, 1);
    }
    std::string integer = text;
    std::string fraction;
    if (const auto dot = text.find('.'); dot != std::string::npos)
    {
        integer  = text.substr(0, dot);
        fraction = text.substr(dot + 1);
    }
    while (static_cast<int>(fraction.size()) < fraction_digits)
    {
        fraction += '0';
    }
    while (static_cast<int>(integer.size()) < integer_digits)
    {
        integer.insert(integer.begin(), '0');
    }
    if (integer.empty())
    {
        integer = "0";
    }
    // A value that rounds to zero loses its sign.
    const bool                 zero   = integer.find_first_not_of('0') == std::string::npos &&
                                        fraction.find_first_not_of('0') == std::string::npos;
    const NumberFormatSymbols& active = effective_symbols();
    std::string                out    = negative && !zero ? "-" : "";
    out += is_grouped ? group_digits(integer, active) : widen(integer);
    if (!fraction.empty() || is_decimal_point_always_displayed)
    {
        out += active.decimal_separator;
        out += widen(fraction);
    }
    return out;
}

std::optional<double> DecimalFormatter::parse(std::string_view text) const
{
    const std::size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::size_t          end     = text.find_last_not_of(" \t");
    const std::string_view     number  = text.substr(begin, end - begin + 1);
    const NumberFormatSymbols& active  = effective_symbols();
    const std::string_view     decimal = active.decimal_separator;
    const std::string_view     group   = active.grouping_separator;
    std::string                narrow;
    bool                       seen_decimal = false;
    for (std::size_t i = 0; i < number.size();)
    {
        const std::string_view rest = number.substr(i);
        if (!decimal.empty() && rest.starts_with(decimal))
        {
            if (seen_decimal)
            {
                return std::nullopt;
            }
            seen_decimal = true;
            narrow += '.';
            i += decimal.size();
            continue;
        }
        if (is_grouped && !group.empty() && rest.starts_with(group) && !seen_decimal)
        {
            i += group.size();
            continue;
        }
        const char ch      = number[i];
        const bool allowed = (ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == 'e' || ch == 'E';
        if (!allowed)
        {
            return std::nullopt;
        }
        narrow += static_cast<char>(ch);
        ++i;
    }
    const char* first = narrow.c_str();
    if (*first == '+')
    {
        ++first;
    }
    double     value  = 0.0;
    const auto result = std::from_chars(first, narrow.c_str() + narrow.size(), value);
    if (result.ec != std::errc{} || result.ptr != narrow.c_str() + narrow.size())
    {
        return std::nullopt;
    }
    return value;
}

// ---- expression parser --------------------------------------------------------------

namespace
{
enum class TokenType
{
    Numeric,
    Operator,
    Parenthesis,
};

struct Token
{
    TokenType type;
    char      ch    = 0;
    double    value = 0.0;
};

constexpr std::string_view Operators = "+-*/^";

bool ends_number(char ch)
{
    return ch == '-' || ch == '+' || ch == '/' || ch == '*' || ch == '(' || ch == ')' || ch == '^' || ch == ' ' ||
           ch == '\t' || ch == '\n' || ch == '\r';
}

int precedence(char ch)
{
    if (ch == '*' || ch == '/')
    {
        return 1;
    }
    if (ch == '^')
    {
        return 2;
    }
    return 0;
}

// Numbers and operators alternate, parentheses anywhere; empty on any error.
std::vector<Token> tokenize(std::string_view input, const DecimalFormatter& parser)
{
    std::vector<Token> tokens;
    bool               expect_number = true;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const char ch = input[i];
        if (ch == ' ')
        {
            continue;
        }
        if (expect_number)
        {
            if (ch == '(')
            {
                tokens.push_back({TokenType::Parenthesis, ch});
                continue;
            }
            // A number: an optional minus, then everything up to an operator, a parenthesis or
            // whitespace, the regex ^-?([^-+/*\(\)\^\s]+)
            std::size_t j = i;
            if (input[j] == '-')
            {
                ++j;
            }
            const std::size_t digits = j;
            while (j < input.size() && !ends_number(input[j]))
            {
                ++j;
            }
            if (j == digits)
            {
                return {};
            }
            const std::optional<double> number = parser.parse(input.substr(i, j - i));
            if (!number)
            {
                return {};
            }
            tokens.push_back({TokenType::Numeric, 0, *number});
            i             = j - 1;
            expect_number = false;
        }
        else if (Operators.find(ch) != std::string_view::npos)
        {
            tokens.push_back({TokenType::Operator, ch});
            expect_number = true;
        }
        else if (ch == ')')
        {
            tokens.push_back({TokenType::Parenthesis, ch});
        }
        else
        {
            return {};
        }
    }
    return tokens;
}

std::vector<Token> to_postfix(const std::vector<Token>& infix)
{
    std::vector<Token> postfix;
    std::stack<Token>  operators;
    for (const Token& token : infix)
    {
        if (token.type == TokenType::Numeric)
        {
            postfix.push_back(token);
        }
        else if (token.type == TokenType::Operator)
        {
            while (!operators.empty() && operators.top().type != TokenType::Parenthesis &&
                   precedence(operators.top().ch) >= precedence(token.ch))
            {
                postfix.push_back(operators.top());
                operators.pop();
            }
            operators.push(token);
        }
        else if (token.ch == '(')
        {
            operators.push(token);
        }
        else
        {
            while (!operators.empty() && operators.top().ch != '(')
            {
                postfix.push_back(operators.top());
                operators.pop();
            }
            if (operators.empty())
            {
                return {}; // broken parenthesis
            }
            operators.pop();
        }
    }
    while (!operators.empty())
    {
        if (operators.top().type == TokenType::Parenthesis)
        {
            return {};
        }
        postfix.push_back(operators.top());
        operators.pop();
    }
    return postfix;
}

std::optional<double> compute(const std::vector<Token>& postfix)
{
    std::stack<double> stack;
    for (const Token& token : postfix)
    {
        if (token.type == TokenType::Numeric)
        {
            stack.push(token.value);
            continue;
        }
        if (stack.size() < 2)
        {
            return std::nullopt;
        }
        const double op1 = stack.top();
        stack.pop();
        const double op2 = stack.top();
        stack.pop();
        double result = 0.0;
        switch (token.ch)
        {
            case '-':
                result = op2 - op1;
                break;
            case '+':
                result = op1 + op2;
                break;
            case '*':
                result = op1 * op2;
                break;
            case '/':
                if (op1 == 0.0)
                {
                    return std::numeric_limits<double>::quiet_NaN();
                }
                result = op2 / op1;
                break;
            case '^':
                result = std::pow(op2, op1);
                break;
            default:
                return std::nullopt;
        }
        stack.push(result);
    }
    if (stack.size() != 1)
    {
        return std::nullopt;
    }
    return stack.top();
}
} // namespace

std::optional<double> evaluate_expression(std::string_view expression, const DecimalFormatter& parser)
{
    const std::vector<Token> tokens = tokenize(expression, parser);
    if (tokens.empty())
    {
        return std::nullopt;
    }
    const std::vector<Token> postfix = to_postfix(tokens);
    if (postfix.empty())
    {
        return std::nullopt;
    }
    return compute(postfix);
}

} // namespace ui
