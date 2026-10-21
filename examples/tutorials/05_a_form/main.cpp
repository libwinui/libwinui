// SPDX-License-Identifier: MIT
// libwinui tutorial: a form.
#include <winui/winui.hpp>

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>

int main()
{
    // --8<-- [start:model]
    const std::vector<std::string> plans{"Free", "Team", "Enterprise"};

    ui::Property<std::string>                name{};
    ui::Property<std::string>                email{};
    ui::Property<std::optional<std::size_t>> plan{0};
    ui::Property<std::optional<double>>      seats{1.0}; // unset while the box is empty
    ui::Property<bool>                       agreed{false};
    ui::Property<std::string>                result{};
    // --8<-- [end:model]

    // --8<-- [start:validation]
    // One check per field: what is wrong with the value, or nothing.
    auto name_problem = [](const std::string& n) { return n.empty() ? std::string{"Enter your name."} : std::string{}; };
    auto email_problem = [](const std::string& e)
    { return e.find('@') == std::string::npos ? std::string{"Enter an email address."} : std::string{}; };
    auto seats_problem = [](const std::optional<std::size_t>& p, const std::optional<double>& s) -> std::string
    {
        if (!s)
        {
            return "Enter a number of seats.";
        }
        if (p == 0 && *s > 1.0)
        {
            return "The Free plan has one seat. Pick Team for more.";
        }
        return {};
    };
    auto terms_problem = [](bool ok) { return ok ? std::string{} : std::string{"Accept the terms to continue."}; };

    // The errors show once Sign up was pressed, not while the form is still being filled in.
    ui::Property<bool> tried{false};

    ui::Computed name_error{[=](bool t, const std::string& n) { return t ? name_problem(n) : std::string{}; }, tried, name};
    ui::Computed email_error{[=](bool t, const std::string& e) { return t ? email_problem(e) : std::string{}; }, tried, email};
    ui::Computed seats_error{[=](bool t, const std::optional<std::size_t>& p, const std::optional<double>& n)
                             { return t ? seats_problem(p, n) : std::string{}; },
                             tried,
                             plan,
                             seats};
    ui::Computed terms_error{[=](bool t, bool ok) { return t ? terms_problem(ok) : std::string{}; }, tried, agreed};
    // --8<-- [end:validation]

    // --8<-- [start:commands]
    ui::Command submit{
        .text = "Sign up",
        .execute =
            [&]
        {
            tried               = true;
            const bool complete = name_problem(name.get()).empty() && email_problem(email.get()).empty() &&
                                  seats_problem(plan.get(), seats.get()).empty() && terms_problem(agreed.get()).empty();
            if (!complete)
            {
                return; // the fields show what is missing
            }
            result = std::format("{} <{}> signed up for {:.0f} seat(s) on the {} plan.",
                                 name.get(),
                                 email.get(),
                                 seats.get().value_or(1.0),
                                 plans[plan.get().value_or(0)]);
        },
    };

    ui::Command reset{
        .text = "Reset",
        .execute =
            [&]
        {
            tried  = false;
            name   = "";
            email  = "";
            plan   = 0;
            seats  = 1.0;
            agreed = false;
            result = "";
        },
    };
    // --8<-- [end:commands]

    ui::Window window{{.title = "A form", .size = {520, 680}}};

    // --8<-- [start:tree]
    window
        .set_content(
            ui::VerticalStack{
                .spacing   = 12,
                .margin    = 24,
                .max_width = 420,
                .content =
                    {
                        ui::Text{.text = "Sign up", .role = ui::TextRole::Title},
                        // --8<-- [start:infobars]
                        ui::InfoBar{
                            .title    = "Almost there",
                            .message  = terms_error,
                            .is_open  = ui::bind(terms_error, [](const std::string& p) { return !p.empty(); }),
                            .severity = ui::InfoBarSeverity::Warning,
                            .closable = false,
                        },
                        ui::InfoBar{
                            .title     = "Done",
                            .message   = result,
                            .is_open   = ui::bind(result, [](const std::string& r) { return !r.empty(); }),
                            .severity  = ui::InfoBarSeverity::Success,
                            .on_closed = [&](ui::PopupCloseReason) { result = ""; },
                        },
                        // --8<-- [end:infobars]
                        // --8<-- [start:inputs]
                        ui::TextBox{
                            .header           = "Name",
                            .error            = name_error,
                            .placeholder_text = "Ada Lovelace",
                            .text             = name,
                        },
                        ui::TextBox{
                            .header           = "Email",
                            .error            = email_error,
                            .placeholder_text = "ada@example.com",
                            .text             = email,
                            .spell_check      = false,
                        },
                        ui::ComboBox{
                            .header         = "Plan",
                            .items          = plans,
                            .selected_index = plan,
                        },
                        ui::NumberBox{
                            .header                = "Seats",
                            .error                 = seats_error,
                            .value                 = seats,
                            .min                   = 1.0,
                            .max                   = 50.0,
                            .spin_button_placement = ui::NumberBoxSpinButtonPlacementMode::Inline,
                        },
                        ui::CheckBox{.text = "I accept the terms", .checked = agreed},
                        // --8<-- [end:inputs]
                        ui::HorizontalStack{
                            .spacing = 8,
                            .content =
                                {
                                    ui::Button{.text = "Sign up", .variant = ui::ButtonVariant::Accent, .command = submit},
                                    ui::Button{.text = "Reset", .command = reset},
                                },
                        },
                    },
            });
    // --8<-- [end:tree]

    return ui::run(window);
}
