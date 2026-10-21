// SPDX-License-Identifier: MIT
#pragma once

#include "winui/core/Geometry.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/Element.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <cstddef>

namespace ui::detail
{

// A list a control draws itself (a ComboBox's drop-down, an AutoSuggestBox's suggestions) as
// automation sees it: a List node over one ListItem per entry. Neither holds state of its own;
// everything is read live through the owner's callbacks. The item nodes are made on first use and
// kept, so a client holding one never sees it freed.
class DrawnListAutomation final : public Element
{
public:
    struct Source
    {
        std::function<std::size_t()>            count;
        std::function<std::string(std::size_t)> name;
        std::function<Rect()>                   bounds; // the list's, in window coordinates
        std::function<Rect(std::size_t)>        row;    // an entry's
        std::function<bool(std::size_t)>        selected;
        std::function<bool(std::size_t)>        select; // SelectionItem's Select; false when it can't
        std::function<bool(std::size_t)>        invoke; // Invoke, when the entry has one
    };

    DrawnListAutomation(Element* owner, Source source) : source_(std::move(source))
    {
        set_parent_of(*this, owner);
    }

    // The node of entry `index`, placed over it.
    [[nodiscard]] Element* item(std::size_t index) const
    {
        auto* self = const_cast<DrawnListAutomation*>(this); // NOLINT(cppcoreguidelines-pro-type-const-cast): a cache
        while (items_.size() <= index)
        {
            items_.push_back(std::make_unique<Item>(self, items_.size()));
        }
        items_[index]->place();
        return items_[index].get();
    }

    // Places the list over the owner's.
    void place()
    {
        LayoutAccess::set_bounds(*this, source_.bounds());
    }

    [[nodiscard]] AutomationRole automation_role() const override
    {
        return AutomationRole::List;
    }
    void automation_children(std::vector<Element*>& out) const override
    {
        for (std::size_t i = 0; i < source_.count(); ++i)
        {
            out.push_back(item(i));
        }
    }

protected:
    [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
    {
        return {};
    }
    void render_override(Renderer&, const RenderContext&) const override
    {
    }

private:
    class Item final : public Element, private SelectionItemProvider, private InvokeProvider
    {
    public:
        Item(DrawnListAutomation* list, std::size_t index) : list_(list), index_(index)
        {
            set_parent_of(*this, list);
        }
        void place()
        {
            LayoutAccess::set_bounds(*this, index_ < list_->source_.count() ? list_->source_.row(index_) : Rect{});
        }
        [[nodiscard]] AutomationRole automation_role() const override
        {
            return AutomationRole::ListItem;
        }
        void automation_children(std::vector<Element*>& out) const override
        {
            (void)out;
        }

    protected:
        [[nodiscard]] std::string automation_name_fallback() const override
        {
            return usable() ? list_->source_.name(index_) : std::string{};
        }
        void automation_providers(AutomationProviders& out) override
        {
            if (!usable())
            {
                return;
            }
            out.selection_item = this;
            if (list_->source_.invoke)
            {
                out.invoke = this;
            }
        }
        [[nodiscard]] Size measure_override(Size, const MeasureContext&) const override
        {
            return {};
        }
        void render_override(Renderer&, const RenderContext&) const override
        {
        }

    private:
        [[nodiscard]] bool usable() const
        {
            return index_ < list_->source_.count();
        }
        [[nodiscard]] bool item_selected() const override
        {
            return usable() && list_->source_.selected(index_);
        }
        bool select_item() override
        {
            return usable() && list_->source_.select(index_);
        }
        bool invoke() override
        {
            return usable() && list_->source_.invoke && list_->source_.invoke(index_);
        }

        DrawnListAutomation* list_;
        std::size_t          index_;
    };

    Source                                     source_;
    mutable std::vector<std::unique_ptr<Item>> items_;
};

} // namespace ui::detail
