// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/render/TextLayout.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Launcher.hpp"
#include "winui/ui/text/Inlines.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cstddef>

namespace ui::detail
{

// A hyperlink inline of a text view (Text, RichText) as automation sees it: a Hyperlink node over
// the link's text that Invoke follows. A view keeps one per place in its link list, made on first
// use and kept, so a client holding one never sees it freed; each reads its link live.
class LinkAutomation final : public Element, private InvokeProvider
{
public:
    struct Source
    {
        std::function<const std::vector<const Inline*>&()> links;  // tag i is links()[i]
        std::function<const TextLayout&()>                 layout; // laid out over those links
        std::function<Point()>                             origin; // where the layout's top-left is drawn
        std::function<float()> shown_height; // how much of the layout this view draws, from its top
        std::function<bool()>  enabled;
    };

    LinkAutomation(Element* owner, Source source, std::size_t index) : source_(std::move(source)), index_(index)
    {
        set_parent_of(*this, owner);
    }

    // Around every piece of the link this view draws.
    void place()
    {
        std::optional<Rect> box;
        if (index_ < source_.links().size())
        {
            const TextLayout& layout = source_.layout();
            const Point       origin = source_.origin();
            const float       shown  = source_.shown_height();
            // fragments() index the paragraphs' runs, flattened.
            std::vector<const TextLayoutRun*> runs;
            for (const TextLayoutParagraph& paragraph : layout.content())
            {
                for (const TextLayoutRun& run : paragraph.runs)
                {
                    runs.push_back(&run);
                }
            }
            for (const TextLayout::Fragment& piece : layout.fragments())
            {
                if (piece.run >= runs.size() || runs[piece.run]->tag != index_ || piece.rect.y >= shown)
                {
                    continue;
                }
                const Rect r{piece.rect.x + origin.x, piece.rect.y + origin.y, piece.rect.width, piece.rect.height};
                if (box)
                {
                    const float left   = std::min(box->x, r.x);
                    const float top    = std::min(box->y, r.y);
                    const float right  = std::max(box->right(), r.right());
                    const float bottom = std::max(box->bottom(), r.bottom());
                    box                = Rect{left, top, right - left, bottom - top};
                }
                else
                {
                    box = r;
                }
            }
        }
        LayoutAccess::set_bounds(*this, box.value_or(Rect{}));
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::Hyperlink;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        (void)out;
    }

protected:
    [[nodiscard]] std::string automation_name_fallback() const override
    {
        return index_ < source_.links().size() ? source_.links()[index_]->text : std::string{};
    }
    void automation_providers(AutomationProviders& out) override
    {
        out.invoke = this;
    }
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    // Follows the link as a click does.
    bool invoke() override
    {
        if (!source_.enabled() || index_ >= source_.links().size())
        {
            return false;
        }
        // Copies: the handler may replace the very inlines it lives in.
        const Inline&               link    = *source_.links()[index_];
        const std::function<void()> handler = link.on_click;
        const std::string           uri     = link.navigate_uri;
        if (handler)
        {
            handler();
        }
        launch_uri(uri);
        return true;
    }

    Source      source_;
    std::size_t index_;
};

// Lists a view's link nodes in `out`, making and placing what is needed: one per link it has now.
inline void list_link_automation(Element&                               owner,
                                 std::vector<std::unique_ptr<Element>>& nodes,
                                 const LinkAutomation::Source&          source,
                                 std::vector<Element*>&                 out)
{
    const std::size_t count = source.links().size();
    while (nodes.size() < count)
    {
        nodes.push_back(std::make_unique<LinkAutomation>(&owner, source, nodes.size()));
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        static_cast<LinkAutomation&>(*nodes[i]).place();
        out.push_back(nodes[i].get());
    }
}

// Places the link nodes made so far, after the view was laid out again.
inline void place_link_automation(const std::vector<std::unique_ptr<Element>>& nodes)
{
    for (const auto& node : nodes)
    {
        static_cast<LinkAutomation&>(*node).place();
    }
}

// What a view shows all of: no height limit.
inline float whole_layout()
{
    return std::numeric_limits<float>::infinity();
}

} // namespace ui::detail
