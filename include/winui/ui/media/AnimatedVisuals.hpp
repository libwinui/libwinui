// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Color.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/render/AnimatedVisualSource.hpp"
#include "winui/render/Renderer.hpp"

#include <string>
#include <unordered_map>

namespace ui
{

/// The base of the stock animated icons. Each is 48x48 DIPs, and render_frame() draws it in
/// opaque black.
///
/// Every stock source is stateless, so one instance can serve any number of icons. AnimatedIcon
/// calls render_icon_frame() with its own foreground color. An AnimatedVisualPlayer calls
/// render_frame().
///
/// Example:
/// @code
/// static const ui::AnimatedSettingsVisualSource settings;
///
/// auto icon = std::make_unique<ui::AnimatedIconControl>();
/// icon->set_source(&settings);
/// icon->set_fallback(ui::IconGlyph{0xE713}); // the static settings glyph
/// icon->set_state("Normal");
///
/// // Or play the whole animation in a player:
/// auto player = std::make_unique<ui::AnimatedVisualPlayerControl>();
/// player->set_source(&settings);
/// @endcode
class LottieIconSource : public AnimatedIconSource
{
public:
    /// Always 48x48 DIPs.
    [[nodiscard]] Size natural_size() const override
    {
        return Size{48.0f, 48.0f};
    }
    /// Draws the frame at `progress` (0..1) in opaque black.
    void render_frame(Renderer& renderer, const Rect& bounds, double progress) const override
    {
        render_icon_frame(renderer, bounds, progress, colors::Black);
    }
};

/// The back arrow, for a Back button.
///
/// Example:
/// @code
/// static const ui::AnimatedBackVisualSource back;
/// ui::AnimatedIcon{.source = &back, .state = std::string{"Normal"}};
/// @endcode
class AnimatedBackVisualSource final : public LottieIconSource
{
public:
    /// The length of the whole animation, from progress 0 to 1, in milliseconds.
    [[nodiscard]] Duration duration() const override;
    /// The named state markers, such as "NormalToPointerOver_Start", and their progress values.
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override;
    /// Draws the frame at `progress` (0..1) into `bounds`, filled and stroked in `foreground`.
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override;
};

/// A small downward chevron.
class AnimatedChevronDownSmallVisualSource final : public LottieIconSource
{
public:
    /// The length of the whole animation, from progress 0 to 1, in milliseconds.
    [[nodiscard]] Duration duration() const override;
    /// The named state markers, such as "NormalToPointerOver_Start", and their progress values.
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override;
    /// Draws the frame at `progress` (0..1) into `bounds`, filled and stroked in `foreground`.
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override;
};

/// A small chevron turning from right to down.
class AnimatedChevronRightDownSmallVisualSource final : public LottieIconSource
{
public:
    /// The length of the whole animation, from progress 0 to 1, in milliseconds.
    [[nodiscard]] Duration duration() const override;
    /// The named state markers, such as "NormalToPointerOver_Start", and their progress values.
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override;
    /// Draws the frame at `progress` (0..1) into `bounds`, filled and stroked in `foreground`.
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override;
};

/// A small chevron turning between up and down.
class AnimatedChevronUpDownSmallVisualSource final : public LottieIconSource
{
public:
    /// The length of the whole animation, from progress 0 to 1, in milliseconds.
    [[nodiscard]] Duration duration() const override;
    /// The named state markers, such as "NormalToPointerOver_Start", and their progress values.
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override;
    /// Draws the frame at `progress` (0..1) into `bounds`, filled and stroked in `foreground`.
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override;
};

/// The magnifying glass, for a search button.
///
/// Example:
/// @code
/// static const ui::AnimatedFindVisualSource find;
/// ui::AnimatedIcon{.source   = &find,
///                  .fallback = ui::symbol_glyph(ui::Symbol::Find),
///                  .state    = std::string{"Normal"}};
/// @endcode
class AnimatedFindVisualSource final : public LottieIconSource
{
public:
    /// The length of the whole animation, from progress 0 to 1, in milliseconds.
    [[nodiscard]] Duration duration() const override;
    /// The named state markers, such as "NormalToPointerOver_Start", and their progress values.
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override;
    /// Draws the frame at `progress` (0..1) into `bounds`, filled and stroked in `foreground`.
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override;
};

/// The three-line navigation menu icon, for a button that opens a navigation pane.
class AnimatedGlobalNavigationButtonVisualSource final : public LottieIconSource
{
public:
    /// The length of the whole animation, from progress 0 to 1, in milliseconds.
    [[nodiscard]] Duration duration() const override;
    /// The named state markers, such as "NormalToPointerOver_Start", and their progress values.
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override;
    /// Draws the frame at `progress` (0..1) into `bounds`, filled and stroked in `foreground`.
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override;
};

/// The settings gear.
class AnimatedSettingsVisualSource final : public LottieIconSource
{
public:
    /// The length of the whole animation, from progress 0 to 1, in milliseconds.
    [[nodiscard]] Duration duration() const override;
    /// The named state markers, such as "NormalToPointerOver_Start", and their progress values.
    [[nodiscard]] const std::unordered_map<std::string, double>& markers() const override;
    /// Draws the frame at `progress` (0..1) into `bounds`, filled and stroked in `foreground`.
    void render_icon_frame(Renderer& renderer, const Rect& bounds, double progress, const Color& foreground) const override;
};

} // namespace ui
