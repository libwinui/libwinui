// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "Win32AutomationProvider.hpp"

#include "Win32String.hpp"
#include "winui/core/Geometry.hpp"
#include "winui/testing/WindowSeams.hpp"
#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>

namespace ui::win32
{
namespace
{

// Asks `provider` through `f`, or answers `fallback` when the element no longer provides the
// pattern (it can stop between a client's calls).
template <typename P, typename Fn, typename R>
auto ask(P* provider, Fn&& f, R fallback) -> decltype(f(*provider))
{
    using Result = decltype(f(*provider));
    return provider != nullptr ? f(*provider) : static_cast<Result>(fallback);
}

long control_type_id(AutomationRole role)
{
    switch (role)
    {
        case AutomationRole::Group:
            return UIA_GroupControlTypeId;
        case AutomationRole::Pane:
            return UIA_PaneControlTypeId;
        case AutomationRole::Button:
            return UIA_ButtonControlTypeId;
        case AutomationRole::CheckBox:
            return UIA_CheckBoxControlTypeId;
        case AutomationRole::RadioButton:
            return UIA_RadioButtonControlTypeId;
        case AutomationRole::Edit:
            return UIA_EditControlTypeId;
        case AutomationRole::Text:
            return UIA_TextControlTypeId;
        case AutomationRole::Image:
            return UIA_ImageControlTypeId;
        case AutomationRole::List:
            return UIA_ListControlTypeId;
        case AutomationRole::ListItem:
            return UIA_ListItemControlTypeId;
        case AutomationRole::ComboBox:
            return UIA_ComboBoxControlTypeId;
        case AutomationRole::Slider:
            return UIA_SliderControlTypeId;
        case AutomationRole::ProgressBar:
            return UIA_ProgressBarControlTypeId;
        case AutomationRole::Tab:
            return UIA_TabControlTypeId;
        case AutomationRole::TabItem:
            return UIA_TabItemControlTypeId;
        case AutomationRole::Tree:
            return UIA_TreeControlTypeId;
        case AutomationRole::TreeItem:
            return UIA_TreeItemControlTypeId;
        case AutomationRole::Menu:
            return UIA_MenuControlTypeId;
        case AutomationRole::MenuItem:
            return UIA_MenuItemControlTypeId;
        case AutomationRole::MenuBar:
            return UIA_MenuBarControlTypeId;
        case AutomationRole::ToolBar:
            return UIA_ToolBarControlTypeId;
        case AutomationRole::Hyperlink:
            return UIA_HyperlinkControlTypeId;
        case AutomationRole::Document:
            return UIA_DocumentControlTypeId;
        case AutomationRole::Header:
            return UIA_HeaderControlTypeId;
        case AutomationRole::HeaderItem:
            return UIA_HeaderItemControlTypeId;
        case AutomationRole::Separator:
            return UIA_SeparatorControlTypeId;
        case AutomationRole::SpinButton:
            return UIA_SpinnerControlTypeId;
        case AutomationRole::Thumb:
            return UIA_ThumbControlTypeId;
        case AutomationRole::SplitButton:
            return UIA_SplitButtonControlTypeId;
        case AutomationRole::DataGrid:
            return UIA_DataGridControlTypeId;
        case AutomationRole::DataItem:
            return UIA_DataItemControlTypeId;
        case AutomationRole::Custom:
            return UIA_CustomControlTypeId;
        case AutomationRole::TitleBar:
            return UIA_TitleBarControlTypeId;
        case AutomationRole::Calendar:
            return UIA_CalendarControlTypeId;
    }
    return UIA_CustomControlTypeId;
}

::ToggleState to_uia_toggle_state(ui::ToggleState state)
{
    switch (state)
    {
        case ui::ToggleState::Off:
            return ::ToggleState_Off;
        case ui::ToggleState::On:
            return ::ToggleState_On;
        case ui::ToggleState::Indeterminate:
            return ::ToggleState_Indeterminate;
    }
    return ::ToggleState_Off;
}

double apply_scroll_amount(double current_percent, ScrollAmount amount)
{
    if (current_percent < 0.0)
    {
        return current_percent; // not scrollable on this axis at all
    }
    switch (amount)
    {
        case ScrollAmount_SmallIncrement:
            return std::min(100.0, current_percent + 5.0);
        case ScrollAmount_LargeIncrement:
            return std::min(100.0, current_percent + 20.0);
        case ScrollAmount_SmallDecrement:
            return std::max(0.0, current_percent - 5.0);
        case ScrollAmount_LargeDecrement:
            return std::max(0.0, current_percent - 20.0);
        case ScrollAmount_NoAmount:
        default:
            return current_percent;
    }
}

} // namespace

Win32AutomationProvider::Win32AutomationProvider(
    HWND                                hwnd,
    ui::Window*                         window,
    ui::Element*                        element,
    std::shared_ptr<AutomationLifetime> lifetime,
    bool                                is_root) :
    hwnd_(hwnd),
    window_(window),
    element_(element),
    lifetime_(std::move(lifetime)),
    generation_(window_->content_generation()),
    is_root_(is_root)
{
}

ui::Element* Win32AutomationProvider::effective_root() const
{
    return window_->has_modal() ? window_->modal() : window_->content();
}

std::vector<ui::Element*> Win32AutomationProvider::children_of(const ui::Element& node) const
{
    std::vector<ui::Element*> children = automation_children(node);
    // A caption the theme draws comes first under the root, where the OS lists its own.
    if (&node == effective_root())
    {
        if (ui::Element* title_bar = ui::testing::seams(*window_).drawn_title_bar())
        {
            children.insert(children.begin(), title_bar);
        }
    }
    return children;
}

ui::Element* Win32AutomationProvider::parent_of(const ui::Element& node) const
{
    if (&node == ui::testing::seams(*window_).drawn_title_bar())
    {
        return effective_root();
    }
    return automation_parent(node);
}

ui::Element* Win32AutomationProvider::sibling(int steps) const
{
    if (is_root_)
    {
        return nullptr;
    }
    ui::Element* parent = parent_of(*element_);
    if (!parent)
    {
        return nullptr;
    }
    const std::vector<ui::Element*> siblings = children_of(*parent);
    const auto                      it       = std::find(siblings.begin(), siblings.end(), element_);
    if (it == siblings.end())
    {
        return nullptr;
    }
    const std::ptrdiff_t index = (it - siblings.begin()) + steps;
    if (index < 0 || static_cast<std::size_t>(index) >= siblings.size())
    {
        return nullptr;
    }
    return siblings[static_cast<std::size_t>(index)];
}

Microsoft::WRL::ComPtr<Win32AutomationProvider> Win32AutomationProvider::wrap(ui::Element* element, bool is_root) const
{
    // The object starts with one reference, which the ComPtr adopts.
    Microsoft::WRL::ComPtr<Win32AutomationProvider> provider;
    provider.Attach(new Win32AutomationProvider(hwnd_, window_, element, lifetime_, is_root));
    return provider;
}

ui::Element* Win32AutomationProvider::hit_test(ui::Element& node, ui::Point client_dips)
{
    if (!node.bounds().contains(client_dips))
    {
        return nullptr;
    }
    // Later siblings are drawn on top, so walk in reverse.
    const std::vector<ui::Element*> children = automation_children(node);
    for (auto it = children.rbegin(); it != children.rend(); ++it)
    {
        if (ui::Element* hit = hit_test(**it, client_dips))
        {
            return hit;
        }
    }
    return &node;
}

// ---- IUnknown ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::QueryInterface(REFIID riid, void** out)
{
    if (!out)
    {
        return E_POINTER;
    }
    *out = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IRawElementProviderSimple)))
    {
        *out = static_cast<IRawElementProviderSimple*>(this);
    }
    else if (IsEqualIID(riid, __uuidof(IRawElementProviderFragment)))
    {
        *out = static_cast<IRawElementProviderFragment*>(this);
    }
    else if (is_root_ && IsEqualIID(riid, __uuidof(IRawElementProviderFragmentRoot)))
    {
        *out = static_cast<IRawElementProviderFragmentRoot*>(this);
    }
    else if (alive_and_current())
    {
        const ui::AutomationProviders providers = ui::automation_providers(*element_);
        if (IsEqualIID(riid, __uuidof(IInvokeProvider)) && (providers.invoke != nullptr))
        {
            *out = static_cast<IInvokeProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(IToggleProvider)) && (providers.toggle != nullptr))
        {
            *out = static_cast<IToggleProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(ISelectionItemProvider)) && (providers.selection_item != nullptr))
        {
            *out = static_cast<ISelectionItemProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(ISelectionProvider)) && (providers.selection != nullptr))
        {
            *out = static_cast<ISelectionProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(IExpandCollapseProvider)) && (providers.expand_collapse != nullptr))
        {
            *out = static_cast<IExpandCollapseProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(IRangeValueProvider)) && (providers.range_value != nullptr))
        {
            *out = static_cast<IRangeValueProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(IValueProvider)) && (providers.value != nullptr))
        {
            *out = static_cast<IValueProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(IScrollProvider)) && (providers.scroll != nullptr))
        {
            *out = static_cast<IScrollProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(IGridProvider)) && (providers.grid != nullptr))
        {
            *out = static_cast<IGridProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(IGridItemProvider)) && (providers.grid_item != nullptr))
        {
            *out = static_cast<IGridItemProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(ITableProvider)) && (providers.table != nullptr))
        {
            *out = static_cast<ITableProvider*>(this);
        }
        else if (IsEqualIID(riid, __uuidof(ITableItemProvider)) && (providers.table_item != nullptr))
        {
            *out = static_cast<ITableItemProvider*>(this);
        }
    }

    if (!*out)
    {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE Win32AutomationProvider::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&refs_));
}

ULONG STDMETHODCALLTYPE Win32AutomationProvider::Release()
{
    const LONG r = InterlockedDecrement(&refs_);
    if (r == 0)
    {
        delete this;
    }
    return static_cast<ULONG>(r);
}

// ---- IRawElementProviderSimple ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_ProviderOptions(ProviderOptions* out)
{
    // UseComThreading: UI Automation calls this object through COM on the thread that created it,
    // the UI thread in OLE's single-threaded apartment (create_host() enters it). Without it every
    // call arrives on one of UI Automation's own threads, beside the UI thread.
    *out = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetPatternProvider(PATTERNID id, IUnknown** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const ui::AutomationProviders providers = ui::automation_providers(*element_);
    bool                          matches   = false;
    switch (id)
    {
        case UIA_InvokePatternId:
            matches = (providers.invoke != nullptr);
            break;
        case UIA_TogglePatternId:
            matches = (providers.toggle != nullptr);
            break;
        case UIA_SelectionItemPatternId:
            matches = (providers.selection_item != nullptr);
            break;
        case UIA_SelectionPatternId:
            matches = (providers.selection != nullptr);
            break;
        case UIA_ExpandCollapsePatternId:
            matches = (providers.expand_collapse != nullptr);
            break;
        case UIA_RangeValuePatternId:
            matches = (providers.range_value != nullptr);
            break;
        case UIA_ValuePatternId:
            matches = (providers.value != nullptr);
            break;
        case UIA_ScrollPatternId:
            matches = (providers.scroll != nullptr);
            break;
        case UIA_GridPatternId:
            matches = (providers.grid != nullptr);
            break;
        case UIA_GridItemPatternId:
            matches = (providers.grid_item != nullptr);
            break;
        case UIA_TablePatternId:
            matches = (providers.table != nullptr);
            break;
        case UIA_TableItemPatternId:
            matches = (providers.table_item != nullptr);
            break;
        default:
            break;
    }
    if (!matches)
    {
        return S_OK; // null out: pattern not supported, not an error
    }
    // This class inherits IUnknown through many bases; go through QueryInterface instead of an
    // ambiguous static_cast.
    return QueryInterface(IID_IUnknown, reinterpret_cast<void**>(out));
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetPropertyValue(PROPERTYID id, VARIANT* out)
{
    VariantInit(out);
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (is_root_ && (id == UIA_ControlTypePropertyId || id == UIA_NamePropertyId))
    {
        // The root is merged with the window's HWND, whose provider says what it is: a Window
        // named after its title, as test tools look for an application's windows.
        return S_OK;
    }
    switch (id)
    {
        case UIA_ControlTypePropertyId:
            out->vt   = VT_I4;
            out->lVal = control_type_id(element_->automation_role());
            break;
        case UIA_NamePropertyId:
        {
            const std::wstring name = widen(element_->automation_name());
            if (!name.empty())
            {
                out->vt      = VT_BSTR;
                out->bstrVal = SysAllocString(name.c_str());
            }
            break;
        }
        case UIA_HelpTextPropertyId:
        {
            // The help text: the longer explanation a screen reader offers after the name.
            const std::wstring help = widen(element_->automation_help_text());
            if (!help.empty())
            {
                out->vt      = VT_BSTR;
                out->bstrVal = SysAllocString(help.c_str());
            }
            break;
        }
        case UIA_IsDataValidForFormPropertyId:
            out->vt      = VT_BOOL;
            out->boolVal = element_->automation_valid() ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_IsEnabledPropertyId:
            // Its own enabled(), a control's own condition and every ancestor's, as for input.
            out->vt      = VT_BOOL;
            out->boolVal = element_->is_enabled() ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_HasKeyboardFocusPropertyId:
            out->vt      = VT_BOOL;
            out->boolVal = (ui::automation_focus(window_->focused()) == element_) ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_IsKeyboardFocusablePropertyId:
            out->vt      = VT_BOOL;
            out->boolVal = element_->focusable() ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_IsContentElementPropertyId:
        case UIA_IsControlElementPropertyId:
            // Always true: decorative and native-host nodes never get a provider.
            out->vt      = VT_BOOL;
            out->boolVal = VARIANT_TRUE;
            break;
        case UIA_PositionInSetPropertyId:
        case UIA_SizeOfSetPropertyId:
        {
            // A member of a choice group (Element::selection_group()): its place among the members
            // in tree order, from 1, and how many there are.
            const void* group = element_->selection_group();
            if (group == nullptr)
            {
                break;
            }
            struct Count
            {
                const void*        group;
                const ui::Element* self;
                int                position = 0;
                int                size     = 0;
                void               visit(ui::Element& e)
                {
                    if (e.selection_group() == group)
                    {
                        ++size;
                        if (&e == self)
                        {
                            position = size;
                        }
                    }
                    e.for_each_child([this](ui::Element& child) { visit(child); });
                }
            } count{group, element_};
            count.visit(*effective_root());
            out->vt   = VT_I4;
            out->lVal = id == UIA_PositionInSetPropertyId ? count.position : count.size;
            break;
        }
        case UIA_AutomationIdPropertyId:
        {
            // Only the id the app gave: an element without one reports none.
            const std::wstring automation_id = widen(element_->accessibility().automation_id);
            if (!automation_id.empty())
            {
                out->vt      = VT_BSTR;
                out->bstrVal = SysAllocString(automation_id.c_str());
            }
            break;
        }
        default:
            break; // VT_EMPTY: no opinion, UIA uses its default
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_HostRawElementProvider(IRawElementProviderSimple** out)
{
    *out = nullptr;
    if (is_root_)
    {
        // Lets UIA merge the native window frame and any NativeControl child HWND with this tree.
        return UiaHostProviderFromHwnd(hwnd_, out);
    }
    return S_OK;
}

// ---- IRawElementProviderFragment ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    ui::Element* target = nullptr;
    switch (direction)
    {
        case NavigateDirection_Parent:
            if (!is_root_)
            {
                target = parent_of(*element_);
            }
            break;
        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild:
        {
            const std::vector<ui::Element*> children = children_of(*element_);
            if (!children.empty())
            {
                target = direction == NavigateDirection_FirstChild ? children.front() : children.back();
            }
            break;
        }
        case NavigateDirection_NextSibling:
            target = sibling(1);
            break;
        case NavigateDirection_PreviousSibling:
            target = sibling(-1);
            break;
        default:
            break;
    }

    if (!target)
    {
        return S_OK;
    }
    Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = wrap(target, false);
    *out                                                     = provider.Detach();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetRuntimeId(SAFEARRAY** out)
{
    *out = nullptr;
    if (is_root_)
    {
        return S_OK; // the root's runtime id comes from the HWND, UIA's convention
    }
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    // VisualId is already the framework's stable per-node id.
    const int  rtid[] = {UiaAppendRuntimeId, static_cast<int>(element_->visual_id())};
    SAFEARRAY* sa     = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!sa)
    {
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < 2; ++i)
    {
        SafeArrayPutElement(sa, &i, const_cast<int*>(&rtid[i]));
    }
    *out = sa;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_BoundingRectangle(UiaRect* out)
{
    *out = UiaRect{};
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const ui::Rect b     = element_->bounds();
    const UINT     dpi   = GetDpiForWindow(hwnd_);
    const float    scale = static_cast<float>(dpi) / 96.0f;
    POINT          top_left{static_cast<int>(b.left() * scale), static_cast<int>(b.top() * scale)};
    ClientToScreen(hwnd_, &top_left);
    out->left   = top_left.x;
    out->top    = top_left.y;
    out->width  = static_cast<double>(b.width * scale);
    out->height = static_cast<double>(b.height * scale);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetEmbeddedFragmentRoots(SAFEARRAY** out)
{
    // No IRawElementProviderHwndOverride: a NativeControl's child HWND answers WM_GETOBJECT itself.
    *out = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::SetFocus()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    window_->focus_element(element_); // best effort, see Window::focus_element()
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    // Created on demand: UIA compares identity through GetRuntimeId(), not provider pointers.
    Microsoft::WRL::ComPtr<Win32AutomationProvider> root = wrap(effective_root(), true);
    *out                                                 = root.Detach();
    return S_OK;
}

// ---- IRawElementProviderFragmentRoot ----

HRESULT STDMETHODCALLTYPE
    Win32AutomationProvider::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    POINT screen_pt{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(hwnd_, &screen_pt);
    const UINT      dpi   = GetDpiForWindow(hwnd_);
    const float     scale = 96.0f / static_cast<float>(dpi);
    const ui::Point client_dips{static_cast<float>(screen_pt.x) * scale, static_cast<float>(screen_pt.y) * scale};

    // The drawn caption lies outside the content, so it is tested first.
    ui::Element* hit = nullptr;
    if (ui::Element* title_bar = ui::testing::seams(*window_).drawn_title_bar())
    {
        hit = hit_test(*title_bar, client_dips);
    }
    if (!hit)
    {
        hit = hit_test(*effective_root(), client_dips);
    }
    if (!hit)
    {
        return S_OK;
    }
    Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = wrap(hit, hit == effective_root());
    *out                                                     = provider.Detach();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetFocus(IRawElementProviderFragment** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    ui::Element* focused = ui::automation_focus(window_->focused()); // the item inside a list or menu
    if (!focused)
    {
        return S_OK;
    }
    Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = wrap(focused, focused == effective_root());
    *out                                                     = provider.Detach();
    return S_OK;
}

// ---- IInvokeProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::Invoke()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (ui::automation_providers(*element_).invoke == nullptr || !element_->is_enabled())
    {
        return UIA_E_ELEMENTNOTENABLED;
    }
    // Invoke returns at once and the click runs in the next tick: a handler can open a dialog or a
    // picker, and the client waits for this call to return.
    ui::Window* const   window     = window_;
    const std::uint64_t generation = generation_;
    window_->post(
        [window, generation, watch = std::make_unique<ui::ElementWatch>(element_)]
        {
            if (ui::Element* element = watch->get(); element != nullptr && window->content_generation() == generation)
            {
                (void)ui::testing::seams(*window).handle_automation_invoke(*element);
            }
        });
    return S_OK;
}

// ---- IToggleProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::Toggle()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(ui::automation_providers(*element_).toggle, [&](ui::ToggleProvider& p) { return p.toggle_next(); }, false) ? S_OK : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_ToggleState(::ToggleState* out)
{
    *out = ::ToggleState_Off;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = to_uia_toggle_state(
        ask(ui::automation_providers(*element_).toggle, [&](ui::ToggleProvider& p) { return p.toggle_value(); }, ui::ToggleState::Off));
    return S_OK;
}

// ---- ISelectionItemProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::Select()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(
               ui::automation_providers(*element_).selection_item,
               [&](ui::SelectionItemProvider& p) { return p.select_item(); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::AddToSelection()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(
               ui::automation_providers(*element_).selection_item,
               [&](ui::SelectionItemProvider& p) { return p.add_to_selection(); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::RemoveFromSelection()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(
               ui::automation_providers(*element_).selection_item,
               [&](ui::SelectionItemProvider& p) { return p.remove_from_selection(); },
               false)
               ? S_OK
               : UIA_E_INVALIDOPERATION; // an entry only selecting another one deselects
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_IsSelected(BOOL* out)
{
    *out = VARIANT_FALSE;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
               ui::automation_providers(*element_).selection_item,
               [&](ui::SelectionItemProvider& p) { return p.item_selected(); },
               false)
               ? VARIANT_TRUE
               : VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_SelectionContainer(IRawElementProviderSimple** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    // The nearest ancestor that advertises Selection; an item's container is always above it.
    for (ui::Element* up = element_->parent(); up; up = up->parent())
    {
        if (ui::automation_providers(*up).selection != nullptr)
        {
            Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = wrap(up, up == effective_root());
            *out                                                     = provider.Detach();
            break;
        }
    }
    return S_OK;
}

// ---- ISelectionProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetSelection(SAFEARRAY** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    std::vector<ui::Element*> selected;
    if (auto* p = ui::automation_providers(*element_).selection)
    {
        p->selected_items(selected);
    }
    return provider_array(selected, out);
}

HRESULT Win32AutomationProvider::provider_array(const std::vector<ui::Element*>& elements, SAFEARRAY** out) const
{
    SAFEARRAY* sa = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(elements.size()));
    if (!sa)
    {
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < static_cast<LONG>(elements.size()); ++i)
    {
        ui::Element*                                    element  = elements[static_cast<std::size_t>(i)];
        Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = wrap(element, element == effective_root());
        IUnknown* unknown = static_cast<IRawElementProviderSimple*>(provider.Get());
        SafeArrayPutElement(sa, &i, unknown); // AddRefs
    }
    *out = sa;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_CanSelectMultiple(BOOL* out)
{
    *out = VARIANT_FALSE;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
               ui::automation_providers(*element_).selection,
               [&](ui::SelectionProvider& p) { return p.can_select_multiple(); },
               false)
               ? VARIANT_TRUE
               : VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_IsSelectionRequired(BOOL* out)
{
    *out = VARIANT_FALSE;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
               ui::automation_providers(*element_).selection,
               [&](ui::SelectionProvider& p) { return p.selection_required(); },
               false)
               ? VARIANT_TRUE
               : VARIANT_FALSE;
    return S_OK;
}

// ---- IExpandCollapseProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::Expand()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(
               ui::automation_providers(*element_).expand_collapse,
               [&](ui::ExpandCollapseProvider& p) { return p.set_expanded_state(true); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::Collapse()
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(
               ui::automation_providers(*element_).expand_collapse,
               [&](ui::ExpandCollapseProvider& p) { return p.set_expanded_state(false); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_ExpandCollapseState(ExpandCollapseState* out)
{
    *out = ExpandCollapseState_Collapsed;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
               ui::automation_providers(*element_).expand_collapse,
               [&](ui::ExpandCollapseProvider& p) { return p.expanded_state(); },
               false)
               ? ExpandCollapseState_Expanded
               : ExpandCollapseState_Collapsed;
    return S_OK;
}

// ---- IRangeValueProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::SetValue(double value)
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(
               ui::automation_providers(*element_).range_value,
               [&](ui::RangeValueProvider& p) { return p.set_range_value(value); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_Value(double* out)
{
    *out = 0.0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out =
        ask(ui::automation_providers(*element_).range_value, [&](ui::RangeValueProvider& p) { return p.range_value(); }, 0.0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_IsReadOnly(BOOL* out)
{
    *out = VARIANT_TRUE;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    // IRangeValueProvider and IValueProvider share this override; no control advertises both.
    // ProgressBar and ProgressRing report writable without a setter, a known simplification.
    const ui::AutomationProviders providers = ui::automation_providers(*element_);
    if ((providers.range_value != nullptr))
    {
        *out = ask(
                   ui::automation_providers(*element_).range_value,
                   [&](ui::RangeValueProvider& p) { return p.range_read_only(); },
                   false)
                   ? VARIANT_TRUE
                   : VARIANT_FALSE;
    }
    else if ((providers.value != nullptr))
    {
        *out = ask(
                   ui::automation_providers(*element_).value,
                   [&](ui::ValueProvider& p) { return p.text_value_read_only(); },
                   true)
                   ? VARIANT_TRUE
                   : VARIANT_FALSE;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_Maximum(double* out)
{
    *out = 0.0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out =
        ask(ui::automation_providers(*element_).range_value, [&](ui::RangeValueProvider& p) { return p.range_maximum(); }, 0.0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_Minimum(double* out)
{
    *out = 0.0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out =
        ask(ui::automation_providers(*element_).range_value, [&](ui::RangeValueProvider& p) { return p.range_minimum(); }, 0.0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_LargeChange(double* out)
{
    *out = 0.0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    // No separate large step is modeled; reuses the small change.
    *out = ask(
        ui::automation_providers(*element_).range_value,
        [&](ui::RangeValueProvider& p) { return p.range_small_change(); },
        0.0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_SmallChange(double* out)
{
    *out = 0.0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
        ui::automation_providers(*element_).range_value,
        [&](ui::RangeValueProvider& p) { return p.range_small_change(); },
        0.0);
    return S_OK;
}

// ---- IValueProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::SetValue(LPCWSTR value)
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    // Sanitized on the way in: narrow() turns an unpaired surrogate into U+FFFD.
    return ask(
               ui::automation_providers(*element_).value,
               [&](ui::ValueProvider& p) { return p.set_text_value(narrow(value)); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_Value(BSTR* out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = SysAllocString(widen(ask(
                                    ui::automation_providers(*element_).value,
                                    [&](ui::ValueProvider& p) { return p.text_value(); },
                                    std::string{}))
                              .c_str());
    return S_OK;
}

// ---- IScrollProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::Scroll(ScrollAmount horizontal, ScrollAmount vertical)
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const double h = apply_scroll_amount(ask(
                                             ui::automation_providers(*element_).scroll,
                                             [&](ui::ScrollProvider& p) { return p.scroll_horizontal_percent(); },
                                             -1.0f),
                                         horizontal);
    const double v = apply_scroll_amount(ask(
                                             ui::automation_providers(*element_).scroll,
                                             [&](ui::ScrollProvider& p) { return p.scroll_vertical_percent(); },
                                             -1.0f),
                                         vertical);
    return ask(
               ui::automation_providers(*element_).scroll,
               [&](ui::ScrollProvider& p) { return p.set_scroll_percent(static_cast<float>(h), static_cast<float>(v)); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::SetScrollPercent(double horizontal, double vertical)
{
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return ask(
               ui::automation_providers(*element_).scroll,
               [&](ui::ScrollProvider& p)
               { return p.set_scroll_percent(static_cast<float>(horizontal), static_cast<float>(vertical)); },
               false)
               ? S_OK
               : UIA_E_ELEMENTNOTENABLED;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_HorizontalScrollPercent(double* out)
{
    *out = -1.0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
        ui::automation_providers(*element_).scroll,
        [&](ui::ScrollProvider& p) { return p.scroll_horizontal_percent(); },
        -1.0f);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_VerticalScrollPercent(double* out)
{
    *out = -1.0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
        ui::automation_providers(*element_).scroll,
        [&](ui::ScrollProvider& p) { return p.scroll_vertical_percent(); },
        -1.0f);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_HorizontalViewSize(double* out)
{
    // No viewport-to-content ratio is tracked; every scrollable control reports the whole viewport.
    *out = 100.0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_VerticalViewSize(double* out)
{
    *out = 100.0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_HorizontallyScrollable(BOOL* out)
{
    *out = VARIANT_FALSE;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
               ui::automation_providers(*element_).scroll,
               [&](ui::ScrollProvider& p) { return p.scroll_horizontal_percent(); },
               -1.0f) >= 0.0f
               ? VARIANT_TRUE
               : VARIANT_FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_VerticallyScrollable(BOOL* out)
{
    *out = VARIANT_FALSE;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = ask(
               ui::automation_providers(*element_).scroll,
               [&](ui::ScrollProvider& p) { return p.scroll_vertical_percent(); },
               -1.0f) >= 0.0f
               ? VARIANT_TRUE
               : VARIANT_FALSE;
    return S_OK;
}

// ---- IGridProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetItem(int row, int column, IRawElementProviderSimple** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (row < 0 || column < 0 ||
        static_cast<std::size_t>(row) >=
            ask(
                ui::automation_providers(*element_).grid,
                [&](ui::GridProvider& p) { return p.grid_row_count(); },
                std::size_t{0}) ||
        static_cast<std::size_t>(column) >=
            ask(ui::automation_providers(*element_).grid, [&](ui::GridProvider& p) { return p.grid_column_count(); }, std::size_t{0}))
    {
        return E_INVALIDARG;
    }
    ui::Element* item = ask(
        ui::automation_providers(*element_).grid,
        [&](ui::GridProvider& p) { return p.grid_item(static_cast<std::size_t>(row), static_cast<std::size_t>(column)); },
        static_cast<ui::Element*>(nullptr));
    if (item == nullptr)
    {
        return S_OK; // a cell of a row out of view has no node
    }
    *out = static_cast<IRawElementProviderSimple*>(wrap(item, false).Detach());
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_RowCount(int* out)
{
    *out = 0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = static_cast<int>(
        ask(ui::automation_providers(*element_).grid, [&](ui::GridProvider& p) { return p.grid_row_count(); }, std::size_t{0}));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_ColumnCount(int* out)
{
    *out = 0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = static_cast<int>(
        ask(ui::automation_providers(*element_).grid, [&](ui::GridProvider& p) { return p.grid_column_count(); }, std::size_t{0}));
    return S_OK;
}

// ---- IGridItemProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_Row(int* out)
{
    *out = 0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = static_cast<int>(
        ask(ui::automation_providers(*element_).grid_item, [&](ui::GridItemProvider& p) { return p.grid_item_row(); }, std::size_t{0}));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_Column(int* out)
{
    *out = 0;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = static_cast<int>(
        ask(ui::automation_providers(*element_).grid_item, [&](ui::GridItemProvider& p) { return p.grid_item_column(); }, std::size_t{0}));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_RowSpan(int* out)
{
    *out = 1;
    return alive_and_current() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_ColumnSpan(int* out)
{
    *out = 1;
    return alive_and_current() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_ContainingGrid(IRawElementProviderSimple** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (ui::Element* grid = ask(
            ui::automation_providers(*element_).grid_item,
            [&](ui::GridItemProvider& p) { return p.grid_item_container(); },
            static_cast<ui::Element*>(nullptr)))
    {
        *out = static_cast<IRawElementProviderSimple*>(wrap(grid, grid == effective_root()).Detach());
    }
    return S_OK;
}

// ---- ITableProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetRowHeaders(SAFEARRAY** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return provider_array({}, out);
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetColumnHeaders(SAFEARRAY** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    std::vector<ui::Element*> headers;
    if (auto* p = ui::automation_providers(*element_).table)
    {
        p->table_column_headers(headers);
    }
    return provider_array(headers, out);
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::get_RowOrColumnMajor(RowOrColumnMajor* out)
{
    *out = RowOrColumnMajor_RowMajor;
    return alive_and_current() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

// ---- ITableItemProvider ----

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetRowHeaderItems(SAFEARRAY** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return provider_array({}, out);
}

HRESULT STDMETHODCALLTYPE Win32AutomationProvider::GetColumnHeaderItems(SAFEARRAY** out)
{
    *out = nullptr;
    if (!alive_and_current())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    std::vector<ui::Element*> headers;
    if (auto* p = ui::automation_providers(*element_).table_item)
    {
        p->table_item_column_headers(headers);
    }
    return provider_array(headers, out);
}

// ---- Win32AutomationEvents ----

bool Win32AutomationEvents::listening() const
{
    return lifetime_->alive && UiaClientsAreListening() != FALSE;
}

Microsoft::WRL::ComPtr<Win32AutomationProvider> Win32AutomationEvents::provider_for(ui::Element& node) const
{
    ui::Element*                                    root = window_->has_modal() ? window_->modal() : window_->content();
    Microsoft::WRL::ComPtr<Win32AutomationProvider> provider;
    provider.Attach(new Win32AutomationProvider(hwnd_, window_, &node, lifetime_, &node == root));
    return provider;
}

void Win32AutomationEvents::focus_changed(ui::Element& node)
{
    const Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = provider_for(node);
    UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider.Get()), UIA_AutomationFocusChangedEventId);
}

namespace
{
// A property value as UIA wants it for `property`: VT_EMPTY when the node has no such pattern.
VARIANT variant_of(ui::AutomationProperty property, const ui::AutomationValue& value)
{
    VARIANT out;
    VariantInit(&out);
    if (std::holds_alternative<std::monostate>(value))
    {
        return out;
    }
    switch (property)
    {
        case ui::AutomationProperty::Name:
        case ui::AutomationProperty::Value:
            out.vt      = VT_BSTR;
            out.bstrVal = SysAllocString(widen(std::get<std::string>(value)).c_str());
            break;
        case ui::AutomationProperty::ToggleState:
        {
            const ui::ToggleState state = std::get<ui::ToggleState>(value);
            out.vt                      = VT_I4;
            out.lVal                    = state == ui::ToggleState::On    ? ToggleState_On
                                          : state == ui::ToggleState::Off ? ToggleState_Off
                                                                          : ToggleState_Indeterminate;
            break;
        }
        case ui::AutomationProperty::ExpandCollapseState:
            out.vt   = VT_I4;
            out.lVal = std::get<bool>(value) ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
            break;
        case ui::AutomationProperty::IsSelected:
            out.vt      = VT_BOOL;
            out.boolVal = std::get<bool>(value) ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case ui::AutomationProperty::RangeValue:
            out.vt     = VT_R8;
            out.dblVal = std::get<double>(value);
            break;
    }
    return out;
}

PROPERTYID property_id(ui::AutomationProperty property)
{
    switch (property)
    {
        case ui::AutomationProperty::Name:
            return UIA_NamePropertyId;
        case ui::AutomationProperty::ToggleState:
            return UIA_ToggleToggleStatePropertyId;
        case ui::AutomationProperty::ExpandCollapseState:
            return UIA_ExpandCollapseExpandCollapseStatePropertyId;
        case ui::AutomationProperty::IsSelected:
            return UIA_SelectionItemIsSelectedPropertyId;
        case ui::AutomationProperty::Value:
            return UIA_ValueValuePropertyId;
        case ui::AutomationProperty::RangeValue:
            return UIA_RangeValueValuePropertyId;
    }
    return UIA_NamePropertyId;
}
} // namespace

void Win32AutomationEvents::property_changed(ui::Element&               node,
                                             ui::AutomationProperty     property,
                                             const ui::AutomationValue& before,
                                             const ui::AutomationValue& after)
{
    const Microsoft::WRL::ComPtr<Win32AutomationProvider> provider  = provider_for(node);
    IRawElementProviderSimple* const                      simple    = provider.Get();
    VARIANT                                               old_value = variant_of(property, before);
    VARIANT                                               new_value = variant_of(property, after);
    UiaRaiseAutomationPropertyChangedEvent(simple, property_id(property), old_value, new_value);
    VariantClear(&old_value);
    VariantClear(&new_value);
    // An item that became selected also says so as an event, which list readers listen for.
    if (property == ui::AutomationProperty::IsSelected && std::holds_alternative<bool>(after) && std::get<bool>(after))
    {
        UiaRaiseAutomationEvent(simple, UIA_SelectionItem_ElementSelectedEventId);
    }
}

void Win32AutomationEvents::menu_opened(ui::Element& menu)
{
    const Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = provider_for(menu);
    UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider.Get()), UIA_MenuOpenedEventId);
}

void Win32AutomationEvents::menu_closed(ui::Element& menu)
{
    const Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = provider_for(menu);
    UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider.Get()), UIA_MenuClosedEventId);
}

void Win32AutomationEvents::structure_changed(ui::Element& node)
{
    const Microsoft::WRL::ComPtr<Win32AutomationProvider> provider = provider_for(node);
    // ChildrenInvalidated names the node whose children changed, by its runtime id.
    int runtime_id[] = {UiaAppendRuntimeId, static_cast<int>(node.visual_id())};
    UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(provider.Get()),
                                  StructureChangeType_ChildrenInvalidated,
                                  runtime_id,
                                  2);
}

} // namespace ui::win32

#endif // _WIN32
