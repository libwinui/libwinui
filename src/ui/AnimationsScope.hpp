// SPDX-License-Identifier: MIT
#pragma once

namespace ui
{

struct Theme;

namespace detail
{

// Sets what animations_enabled() answers for as long as it lives, restoring the previous answer
// after. Window opens one around everything it does to its tree.
class AnimationsScope
{
public:
    explicit AnimationsScope(bool enabled);
    ~AnimationsScope();
    AnimationsScope(const AnimationsScope&)            = delete;
    AnimationsScope& operator=(const AnimationsScope&) = delete;

private:
    bool previous_;
};

// With `theme.animations_enabled` off, zeroes the durations controls hand the compositor and the
// popup motion, so nothing that reads them from the theme animates either.
void apply_animations_switch(Theme& theme);

} // namespace detail
} // namespace ui
