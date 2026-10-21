// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "winui/ui/SingleInstance.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ui::win32
{

// The command line a second start sends: the working directory and the arguments, UTF-8, each
// ended by a NUL.
[[nodiscard]] std::string                       encode_activation(const InstanceActivation& activation);
[[nodiscard]] std::optional<InstanceActivation> decode_activation(std::string_view data);

// What a second start of `id` does: finds the instance holding the claim and sends it
// `activation`. False when no instance answered within a few seconds.
bool forward_to_instance(std::string_view id, const InstanceActivation& activation);

// Gives up this process's claim, so a test can claim again.
void release_single_instance();

} // namespace ui::win32

#endif // _WIN32
