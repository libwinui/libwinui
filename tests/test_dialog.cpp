// SPDX-License-Identifier: MIT
#include "winui/core/Geometry.hpp"
#include "winui/core/Input.hpp"
#include "winui/testing/ElementAccess.hpp"
#include "winui/testing/HeadlessUI.hpp"
#include "winui/testing/TestRenderer.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Window.hpp"
#include "winui/ui/dialogs/Dialog.hpp"
#include "winui/ui/input/Button.hpp"
#include "winui/ui/input/CheckBox.hpp"
#include "winui/ui/panels/Stack.hpp"
#include "winui/ui/spec/Control.hpp"
#include "winui/ui/text/AutoSuggestBox.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace std::chrono_literals;

using namespace ui;
using namespace ui::testing;
using Catch::Matchers::WithinAbs;

namespace
{
PointerEvent down(Point p)
{
    return {p, PointerButton::Left, PointerAction::Down};
}
PointerEvent up(Point p)
{
    return {p, PointerButton::Left, PointerAction::Up};
}

struct ContentDialogWindow
{
    HeadlessTextMeasurer measurer;
    Window               window;
    ButtonControl*       background_button;
    int                  background_clicks = 0;

    ContentDialogWindow()
    {
        auto stack        = std::make_unique<StackControl>(Orientation::Vertical);
        background_button = stack->add(std::make_unique<ButtonControl>("Background"));
        background_button->on_click([this] { ++background_clicks; });
        window.set_content(std::move(stack));
        window.layout({400.0f, 400.0f}, measurer);
    }

    void relayout()
    {
        window.layout({400.0f, 400.0f}, measurer);
    }
};
} // namespace

TEST_CASE("New DialogControl has an empty title and no buttons", "[content_dialog]")
{
    DialogControl d;
    REQUIRE(d.title().empty());
    REQUIRE(d.primary_button() == nullptr);
    REQUIRE(d.secondary_button() == nullptr);
    REQUIRE(d.close_button() == nullptr);
}

TEST_CASE("set_title round-trips", "[content_dialog]")
{
    DialogControl d;
    d.set_title("Delete file?");
    REQUIRE(d.title() == "Delete file?");
}

TEST_CASE("Each button and Escape close the dialog by themselves and report their result", "[content_dialog][modal]")
{
    enum class Via
    {
        Primary,
        Secondary,
        Close,
        Escape
    };
    const auto [via, expected] = GENERATE(std::pair{Via::Primary, DialogResult::Primary},
                                          std::pair{Via::Secondary, DialogResult::Secondary},
                                          std::pair{Via::Close, DialogResult::Close},
                                          std::pair{Via::Escape, DialogResult::Close});
    ContentDialogWindow       w;
    int                       clicks = 0;
    std::vector<DialogResult> closed;
    Control<Dialog>           handle;
    w.window.show_modal(Dialog{
        .primary   = {.text = "Save", .on_click = [&] { ++clicks; }},
        .secondary = {.text = "Don't Save", .on_click = [&] { ++clicks; }},
        .close     = {.text = "Cancel", .on_click = [&] { ++clicks; }},
        .on_closed = [&](DialogResult result) { closed.push_back(result); },
        .control   = handle,
    });
    DialogControl* d = handle;
    w.relayout();

    if (via == Via::Escape)
    {
        ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    }
    else
    {
        ButtonControl* button = via == Via::Primary     ? d->primary_button()
                                : via == Via::Secondary ? d->secondary_button()
                                                        : d->close_button();
        ui::testing::seams(w.window).handle_pointer(down(button->bounds().center()));
        ui::testing::seams(w.window).handle_pointer(up(button->bounds().center()));
    }
    CHECK(clicks == 1); // Escape clicks the Close button
    CHECK_FALSE(w.window.has_modal());
    CHECK(closed == std::vector{expected});
}

TEST_CASE("on_closed fires once the dialog is out of the window, and may show the next one", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    ui::testing::seams(w.window).handle_pointer(down(w.background_button->bounds().center()));
    ui::testing::seams(w.window).handle_pointer(up(w.background_button->bounds().center()));
    REQUIRE(w.window.focused() == w.background_button);

    bool            gone_when_closed = false;
    bool            focus_restored   = false;
    Control<Dialog> handle;
    w.window.show_modal(Dialog{
        .primary = {.text = "OK"},
        .on_closed =
            [&](DialogResult)
        {
            gone_when_closed = !w.window.has_modal();
            focus_restored   = w.window.focused() == w.background_button;
            w.window.show_modal(Dialog{.title = "Next", .close = {.text = "Close"}});
        },
        .control = handle,
    });
    DialogControl* d = handle;
    w.relayout();

    ui::testing::seams(w.window).handle_pointer(down(d->primary_button()->bounds().center()));
    ui::testing::seams(w.window).handle_pointer(up(d->primary_button()->bounds().center()));
    CHECK(gone_when_closed);
    CHECK(focus_restored);
    REQUIRE(w.window.has_modal());
    CHECK(static_cast<DialogControl*>(w.window.modal())->title() == "Next");
}

TEST_CASE("transition_opacity() fades in over on_tick(), with no matching close-side fade",
          "[content_dialog][animation]")
{
    DialogControl d;
    REQUIRE(d.transition_opacity() == 0.0f); // never ticked yet: fully faded out

    d.tick(125ms); // halfway through the 250ms open fade
    REQUIRE_THAT(d.transition_opacity(), WithinAbs(0.5f, 0.01f));

    d.tick(125ms); // fade complete
    REQUIRE(d.transition_opacity() == 1.0f);

    d.tick(1000ms); // steady state: further ticks don't overshoot past 1
    REQUIRE(d.transition_opacity() == 1.0f);
}

TEST_CASE("show_modal blocks every pointer event from reaching content_, with no light-dismiss",
          "[content_dialog][modal]")
{
    ContentDialogWindow w;
    auto                dialog = std::make_unique<DialogControl>();
    dialog->set_title("Delete file?");
    int primary_clicks = 0;
    dialog->set_primary_button("Delete", [&] { ++primary_clicks; });
    DialogControl* d = w.window.show_modal(std::move(dialog));
    w.relayout();

    REQUIRE(w.window.has_modal());
    REQUIRE(w.window.modal() == d);

    const Point background_pt = w.background_button->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(background_pt));
    ui::testing::seams(w.window).handle_pointer(up(background_pt));
    REQUIRE(w.background_clicks == 0);
    REQUIRE(w.window.has_modal()); // still open

    const Point primary_pt = d->primary_button()->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(primary_pt));
    ui::testing::seams(w.window).handle_pointer(up(primary_pt));
    REQUIRE(primary_clicks == 1);
}

TEST_CASE("Tab is trapped within the open modal's own focusables", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    auto                dialog = std::make_unique<DialogControl>();
    dialog->set_primary_button("OK", nullptr);
    dialog->set_close_button("Cancel", nullptr);
    DialogControl* d = w.window.show_modal(std::move(dialog));
    w.relayout();

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(w.window.focused() != nullptr);
        REQUIRE((w.window.focused() == d->primary_button() || w.window.focused() == d->close_button()));
        REQUIRE(w.window.focused() != w.background_button);
        ui::testing::seams(w.window).handle_key({Key::Tab, KeyAction::Down});
    }
}

TEST_CASE("Escape closes a dialog without a Close button with None", "[content_dialog][modal]")
{
    ContentDialogWindow       w;
    std::vector<DialogResult> closed;
    auto                      dialog = std::make_unique<DialogControl>();
    dialog->on_closed([&](DialogResult result) { closed.push_back(result); });
    w.window.show_modal(std::move(dialog));
    w.relayout();

    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    CHECK_FALSE(w.window.has_modal());
    CHECK(closed == std::vector{DialogResult::Close});
}

TEST_CASE("A button closing its own dialog doesn't crash, and focus comes back", "[content_dialog][modal]")
{
    // The close is asked for while this Button's handle_pointer() is still on the stack, so the
    // window has to defer destroying it.
    ContentDialogWindow w;
    ui::testing::seams(w.window).handle_pointer(down(w.background_button->bounds().center()));
    ui::testing::seams(w.window).handle_pointer(up(w.background_button->bounds().center()));
    REQUIRE(w.window.focused() == w.background_button);

    auto dialog         = std::make_unique<DialogControl>();
    int  primary_clicks = 0;
    dialog->set_primary_button("Delete", [&] { ++primary_clicks; });
    DialogControl* d = w.window.show_modal(std::move(dialog));
    w.relayout();

    const Point p = d->primary_button()->bounds().center();
    ui::testing::seams(w.window).handle_pointer(down(p));
    ui::testing::seams(w.window).handle_pointer(up(p)); // must not crash

    REQUIRE(primary_clicks == 1);
    REQUIRE_FALSE(w.window.has_modal());
    REQUIRE(w.window.focused() == w.background_button);
}

TEST_CASE("Buttons sit Primary, Secondary, Close whatever order they were set in", "[content_dialog][layout]")
{
    // The dialog's button row: a 320-wide dialog leaves 270 between the 24px paddings inside the 1px frame.
    HeadlessTextMeasurer measurer;
    Window               window;
    auto                 owned = std::make_unique<DialogControl>();
    owned->set_close_button("C", nullptr);
    owned->set_secondary_button("S", nullptr);
    owned->set_primary_button("P", nullptr);
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    DialogControl* d = window.set_content(std::move(owned));
    window.layout({400.0f, 800.0f}, measurer);

    REQUIRE_THAT(d->bounds().width, WithinAbs(320.0f, 0.01));
    const Rect p = d->primary_button()->bounds();
    const Rect s = d->secondary_button()->bounds();
    const Rect c = d->close_button()->bounds();
    CHECK(p.x < s.x);
    CHECK(s.x < c.x);
    // All three buttons: three equal star columns with the two 8px spacers.
    CHECK_THAT(p.width, WithinAbs((270.0f - 16.0f) / 3.0f, 0.01));
    CHECK_THAT(s.x - p.right(), WithinAbs(8.0f, 0.01));
    CHECK_THAT(c.right(), WithinAbs(d->bounds().right() - 25.0f, 0.01));
}

TEST_CASE("One button takes the right half; two split the row", "[content_dialog][layout]")
{
    HeadlessTextMeasurer measurer;
    {
        Window window;
        window.set_stretch_content(false); // laid out for the control at its desired size, centered
        DialogControl* d = window.set_content(std::make_unique<DialogControl>());
        d->set_primary_button("OK", nullptr);
        window.layout({400.0f, 800.0f}, measurer);
        const Rect b = d->primary_button()->bounds();
        CHECK_THAT(b.width, WithinAbs(131.0f, 0.01)); // the masters' (270 - 8) / 2
        CHECK_THAT(b.right(), WithinAbs(d->bounds().right() - 25.0f, 0.01));
    }
    {
        Window window;
        window.set_stretch_content(false); // laid out for the control at its desired size, centered
        DialogControl* d = window.set_content(std::make_unique<DialogControl>());
        d->set_secondary_button("Keep", nullptr);
        d->set_close_button("Cancel", nullptr);
        window.layout({400.0f, 800.0f}, measurer);
        CHECK_THAT(d->secondary_button()->bounds().x, WithinAbs(d->bounds().x + 25.0f, 0.01));
        CHECK_THAT(d->close_button()->bounds().width, WithinAbs(131.0f, 0.01));
    }
}

TEST_CASE("A DialogControl is 320x184 at the least", "[content_dialog][layout]")
{
    HeadlessTextMeasurer measurer;
    Window               window;
    window.set_stretch_content(false); // laid out for the control at its desired size, centered
    DialogControl* d = window.set_content(std::make_unique<DialogControl>());
    d->set_primary_button("OK", nullptr);
    window.layout({400.0f, 800.0f}, measurer);
    CHECK_THAT(d->bounds().width, WithinAbs(320.0f, 0.01));
    CHECK_THAT(d->bounds().height, WithinAbs(184.0f, 0.01));
    // The command area is 24 + 32 + 24 against the bottom edge.
    CHECK_THAT(d->primary_button()->bounds().bottom(), WithinAbs(d->bounds().bottom() - 25.0f, 0.5));
}

TEST_CASE("on_closing sees each close's result and can cancel it, and on_closed waits", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    auto                dialog = std::make_unique<DialogControl>();
    dialog->set_primary_button("Save", nullptr);
    dialog->set_secondary_button("Don't Save", nullptr);
    dialog->set_close_button("Cancel", nullptr);
    std::vector<DialogResult> seen;
    std::vector<DialogResult> closed;
    bool                      veto = true;
    dialog->on_closing(
        [&](const DialogClosingArgs& args)
        {
            seen.push_back(args.result);
            return veto ? ui::Cancel : ui::Proceed;
        });
    dialog->on_closed([&](DialogResult result) { closed.push_back(result); });
    DialogControl* d = w.window.show_modal(std::move(dialog));
    w.relayout();

    const auto click = [&](ButtonControl* b)
    {
        ui::testing::seams(w.window).handle_pointer(down(b->bounds().center()));
        ui::testing::seams(w.window).handle_pointer(up(b->bounds().center()));
    };
    click(d->primary_button());
    click(d->secondary_button());
    click(d->close_button());
    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    REQUIRE(seen == std::vector{DialogResult::Primary, DialogResult::Secondary, DialogResult::Close, DialogResult::Close});
    CHECK(w.window.has_modal()); // every one canceled
    CHECK(closed.empty());

    veto = false;
    click(d->secondary_button());
    CHECK_FALSE(w.window.has_modal());
    CHECK(closed == std::vector{DialogResult::Secondary});
}

TEST_CASE("Escape clicks the Close button when there is one", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    auto                dialog       = std::make_unique<DialogControl>();
    int                 close_clicks = 0;
    dialog->set_primary_button("Save", nullptr);
    dialog->set_close_button("Cancel", [&] { ++close_clicks; });
    DialogResult result = DialogResult::Primary;
    dialog->on_closed([&](DialogResult r) { result = r; });
    w.window.show_modal(std::move(dialog));
    w.relayout();

    ui::testing::seams(w.window).handle_key({Key::Escape, KeyAction::Down});
    CHECK(close_clicks == 1);
    CHECK(result == DialogResult::Close);
}

TEST_CASE("default_button is accented and Enter clicks it from a control that ignores Enter", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    auto                dialog = std::make_unique<DialogControl>();
    auto* check = dialog->set_content(std::make_unique<CheckBoxControl>("Upload your content to the cloud."));
    int   saves = 0;
    dialog->set_primary_button("Save", [&] { ++saves; });
    dialog->set_secondary_button("Don't Save", nullptr);
    dialog->set_close_button("Cancel", nullptr);
    dialog->set_default_button(DialogDefaultButton::Primary);
    DialogResult result = DialogResult::None;
    dialog->on_closed([&](DialogResult r) { result = r; });
    DialogControl* d = w.window.show_modal(std::move(dialog));
    w.relayout();

    CHECK(d->primary_button()->variant() == ButtonVariant::Accent);
    CHECK(d->secondary_button()->variant() == ButtonVariant::Neutral);
    // The initial focus goes to the content first.
    REQUIRE(w.window.focused() == check);

    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Down});
    CHECK(saves == 1);
    CHECK(result == DialogResult::Primary);
}

TEST_CASE("Enter on another button clicks that button, and the accent follows focus", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    auto                dialog  = std::make_unique<DialogControl>();
    int                 saves   = 0;
    int                 cancels = 0;
    dialog->set_primary_button("Save", [&] { ++saves; });
    dialog->set_close_button("Cancel", [&] { ++cancels; });
    dialog->set_default_button(DialogDefaultButton::Primary);
    DialogControl* d = w.window.show_modal(std::move(dialog));
    w.relayout();
    REQUIRE(w.window.focused() == d->primary_button()); // no content: the default button

    ui::testing::seams(w.window).handle_key({Key::Tab, KeyAction::Down});
    REQUIRE(w.window.focused() == d->close_button());
    (void)w.window.tick(16ms); // the accent is re-derived per tick
    CHECK(d->primary_button()->variant() == ButtonVariant::Neutral);

    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Down});
    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Up});
    CHECK(cancels == 1);
    CHECK(saves == 0);
}

TEST_CASE("Enter does nothing with no default button, or a disabled one", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    auto                dialog = std::make_unique<DialogControl>();
    (void)dialog->set_content(std::make_unique<CheckBoxControl>("Check"));
    int            saves = 0;
    ButtonControl* save  = dialog->set_primary_button("Save", [&] { ++saves; });
    dialog->set_default_button(DialogDefaultButton::None);
    DialogControl* d = w.window.show_modal(std::move(dialog));
    w.relayout();

    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Down});
    CHECK(saves == 0);
    CHECK(d->primary_button()->variant() == ButtonVariant::Neutral);

    d->set_default_button(DialogDefaultButton::Primary);
    save->set_enabled(false);
    ui::testing::seams(w.window).handle_key({Key::Enter, KeyAction::Down});
    CHECK(saves == 0);
}

TEST_CASE("An unset default button is the primary button when there is one", "[content_dialog][modal]")
{
    DialogControl dialog;
    CHECK(dialog.default_button() == DialogDefaultButton::None);
    (void)dialog.set_close_button("Cancel", nullptr);
    CHECK(dialog.default_button() == DialogDefaultButton::None);
    ButtonControl* save = dialog.set_primary_button("Save", nullptr);
    CHECK(dialog.default_button() == DialogDefaultButton::Primary);
    CHECK(save->variant() == ButtonVariant::Accent);

    dialog.set_default_button(DialogDefaultButton::Close);
    CHECK(dialog.default_button() == DialogDefaultButton::Close);
    CHECK(save->variant() == ButtonVariant::Neutral);
    dialog.set_default_button(std::nullopt);
    CHECK(dialog.default_button() == DialogDefaultButton::Primary);
}

TEST_CASE("close_modal() restores focus to whatever was focused beforehand", "[content_dialog][modal]")
{
    ContentDialogWindow w;
    ui::testing::seams(w.window).handle_pointer(down(w.background_button->bounds().center()));
    ui::testing::seams(w.window).handle_pointer(up(w.background_button->bounds().center()));
    REQUIRE(w.window.focused() == w.background_button);

    auto dialog = std::make_unique<DialogControl>();
    dialog->set_primary_button("OK", nullptr);
    std::vector<DialogResult> closed;
    dialog->on_closed([&](DialogResult result) { closed.push_back(result); });
    w.window.show_modal(std::move(dialog));
    w.relayout();
    REQUIRE(w.window.focused() != w.background_button);

    w.window.close_modal();
    REQUIRE_FALSE(w.window.has_modal());
    REQUIRE(w.window.focused() == w.background_button);
    CHECK(closed == std::vector{DialogResult::None});
}

TEST_CASE("A max width or width set on a dialog replaces its 548 cap", "[content_dialog][layout]")
{
    HeadlessTextMeasurer measurer;
    // Content that wants 900 DIPs: a plain dialog stops at 548, one allowed 960 grows to fit it.
    auto wide_dialog = [](std::optional<float> max_width, std::optional<float> width)
    {
        auto d = std::make_unique<DialogControl>();
        auto c = std::make_unique<StackControl>(Orientation::Horizontal);
        c->set_width(900.0f);
        c->set_height(40.0f);
        d->set_content(std::move(c));
        d->set_max_width(max_width);
        d->set_width(width);
        return d;
    };

    Window plain;
    plain.set_stretch_content(false);
    DialogControl* a = plain.set_content(wide_dialog(std::nullopt, std::nullopt));
    plain.layout({1400.0f, 800.0f}, measurer);
    CHECK_THAT(a->bounds().width, WithinAbs(548.0f, 0.01));

    Window allowed;
    allowed.set_stretch_content(false);
    DialogControl* b = allowed.set_content(wide_dialog(960.0f, std::nullopt));
    allowed.layout({1400.0f, 800.0f}, measurer);
    CHECK_THAT(b->bounds().width, WithinAbs(900.0f + 2.0f * 25.0f, 0.01)); // 24 padding + 1 frame each side

    Window fixed;
    fixed.set_stretch_content(false);
    DialogControl* c = fixed.set_content(wide_dialog(std::nullopt, 700.0f));
    fixed.layout({1400.0f, 800.0f}, measurer);
    CHECK_THAT(c->bounds().width, WithinAbs(700.0f, 0.01));
}

TEST_CASE("A dialog wider than the standard keeps its buttons at their size, at the right", "[content_dialog][layout]")
{
    HeadlessTextMeasurer measurer;
    auto                 two_buttons = [](std::optional<float> width)
    {
        auto d = std::make_unique<DialogControl>();
        d->set_primary_button("Save", nullptr);
        d->set_close_button("Cancel", nullptr);
        d->set_width(width);
        return d;
    };

    Window standard;
    standard.set_stretch_content(false);
    DialogControl* a = standard.set_content(two_buttons(548.0f));
    standard.layout({1400.0f, 800.0f}, measurer);

    Window wide;
    wide.set_stretch_content(false);
    DialogControl* b = wide.set_content(two_buttons(980.0f));
    wide.layout({1400.0f, 800.0f}, measurer);

    CHECK_THAT(b->primary_button()->bounds().width, WithinAbs(a->primary_button()->bounds().width, 0.01));
    CHECK_THAT(b->close_button()->bounds().width, WithinAbs(a->close_button()->bounds().width, 0.01));
    CHECK_THAT(b->close_button()->bounds().right(), WithinAbs(b->bounds().right() - 25.0f, 0.01)); // 24 padding + 1 frame
    CHECK(b->primary_button()->bounds().x > b->bounds().x + 400.0f);
}

TEST_CASE("A popup opened from inside the dialog draws over the dialog and its scrim", "[content_dialog][modal][popup]")
{
    ContentDialogWindow         w;
    Control<ui::AutoSuggestBox> box;
    w.window.show_modal(Dialog{.title = "Go to", .content = ui::AutoSuggestBox{.control = box}});
    w.relayout();
    ui::testing::set_focused(*box, true);
    box->handle_text("S");
    box->set_suggestions({"Seattle"});
    w.relayout();
    REQUIRE(box->is_suggestion_list_open());

    TestRenderer r;
    w.window.render(r);
    std::optional<std::size_t> scrim;
    std::optional<std::size_t> suggestion;
    for (std::size_t i = 0; i < r.commands.size(); ++i)
    {
        if (const auto* fill = std::get_if<FillRoundedRect>(&r.commands[i]);
            fill != nullptr && fill->bounds == Rect{0.0f, 0.0f, 400.0f, 400.0f})
        {
            scrim = i;
        }
        if (const auto* text = std::get_if<DrawTextRun>(&r.commands[i]); text != nullptr && text->text == "Seattle")
        {
            suggestion = i;
        }
    }
    REQUIRE(scrim);
    REQUIRE(suggestion);
    CHECK(*suggestion > *scrim);
}
