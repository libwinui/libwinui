// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "winui/ui/Automation.hpp"
#include "winui/ui/AutomationTree.hpp"
#include "winui/ui/Element.hpp"
#include "winui/ui/Window.hpp"

#include <windows.h>

// UIAutomationCore.h redefines its own interfaces under WIN32_LEAN_AND_MEAN unless
// <objbase.h> (which defines the `interface` macro) comes first.

// clang-format off
#include <objbase.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
// clang-format on

// The UIA_*Id constants live in the client header, as in the SDK's provider samples.
#include <UIAutomationClient.h>
#include <memory>
#include <wrl/client.h>

#include <cstdint>

namespace ui::win32
{

// Flipped false at WM_DESTROY, before the Window dies: the case content_generation() cannot
// cover, since there is no Window left to ask.
struct AutomationLifetime
{
    bool alive = true;
};

class Win32AutomationProvider final :
    public IRawElementProviderSimple,
    public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot,
    public IInvokeProvider,
    public IToggleProvider,
    public ISelectionItemProvider,
    public ISelectionProvider,
    public IExpandCollapseProvider,
    public IRangeValueProvider,
    public IValueProvider,
    public IScrollProvider,
    public IGridProvider,
    public IGridItemProvider,
    public ITableProvider,
    public ITableItemProvider
{
public:
    // `is_root`: the instance for the window's effective root (the modal or the content), the one
    // WM_GETOBJECT returns. `element` must be non-null.
    Win32AutomationProvider(HWND                                hwnd,
                            ui::Window*                         window,
                            ui::Element*                        element,
                            std::shared_ptr<AutomationLifetime> lifetime,
                            bool                                is_root);

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE   AddRef() override;
    ULONG STDMETHODCALLTYPE   Release() override;

    // ---- IRawElementProviderSimple ----
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** out) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override;

    // ---- IRawElementProviderFragment ----
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;

    // ---- IRawElementProviderFragmentRoot (only reachable via QueryInterface when is_root_) ----
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override;

    // ---- IInvokeProvider ----
    HRESULT STDMETHODCALLTYPE Invoke() override;

    // ---- IToggleProvider ----
    HRESULT STDMETHODCALLTYPE Toggle() override;
    HRESULT STDMETHODCALLTYPE get_ToggleState(::ToggleState* out) override;

    // ---- ISelectionItemProvider ----
    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out) override;

    // ---- ISelectionProvider (no control advertises it yet) ----
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* out) override;

    // ---- IExpandCollapseProvider ----
    HRESULT STDMETHODCALLTYPE Expand() override;
    HRESULT STDMETHODCALLTYPE Collapse() override;
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* out) override;

    // ---- IRangeValueProvider ----
    HRESULT STDMETHODCALLTYPE SetValue(double value) override;
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override;
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override;
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override;
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override;
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override;

    // ---- IValueProvider ----
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override;
    // get_IsReadOnly() is shared with IRangeValueProvider; both declare the same signature.

    // ---- IScrollProvider ----
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override;
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override;
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* out) override;
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* out) override;
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* out) override;
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* out) override;
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* out) override;

    // ---- IGridProvider ----
    HRESULT STDMETHODCALLTYPE GetItem(int row, int column, IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE get_RowCount(int* out) override;
    HRESULT STDMETHODCALLTYPE get_ColumnCount(int* out) override;

    // ---- IGridItemProvider ----
    HRESULT STDMETHODCALLTYPE get_Row(int* out) override;
    HRESULT STDMETHODCALLTYPE get_Column(int* out) override;
    HRESULT STDMETHODCALLTYPE get_RowSpan(int* out) override;
    HRESULT STDMETHODCALLTYPE get_ColumnSpan(int* out) override;
    HRESULT STDMETHODCALLTYPE get_ContainingGrid(IRawElementProviderSimple** out) override;

    // ---- ITableProvider ----
    HRESULT STDMETHODCALLTYPE GetRowHeaders(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE GetColumnHeaders(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE get_RowOrColumnMajor(RowOrColumnMajor* out) override;

    // ---- ITableItemProvider ----
    HRESULT STDMETHODCALLTYPE GetRowHeaderItems(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE GetColumnHeaderItems(SAFEARRAY** out) override;

private:
    // The root stands for the window, which UI Automation keeps one provider for: it follows the
    // tree the window shows now, the content or a modal over it, and stays valid as they change.
    // Any other provider is retired with the tree it was made in.
    [[nodiscard]] bool alive_and_current() const
    {
        if (!lifetime_->alive)
        {
            return false;
        }
        if (is_root_)
        {
            element_ = effective_root();
            return element_ != nullptr;
        }
        return window_->content_generation() == generation_;
    }
    // Recomputed on every call, since a modal can open or close during a provider's life.
    [[nodiscard]] ui::Element* effective_root() const;
    // The sibling `steps` away (negative = previous) in automation_parent(*element_)'s own
    // automation_children() list, or nullptr at either end / for the root.
    [[nodiscard]] ui::Element* sibling(int steps) const;
    // automation_children(), with the drawn caption (Window::drawn_title_bar()) first under the root.
    [[nodiscard]] std::vector<ui::Element*> children_of(const ui::Element& node) const;
    // automation_parent(), with the root as the drawn caption's parent.
    [[nodiscard]] ui::Element*                                    parent_of(const ui::Element& node) const;
    [[nodiscard]] Microsoft::WRL::ComPtr<Win32AutomationProvider> wrap(ui::Element* element, bool is_root) const;
    // A SAFEARRAY of providers for `elements`, as GetSelection and the header getters return.
    HRESULT provider_array(const std::vector<ui::Element*>& elements, SAFEARRAY** out) const;
    // Recursive automation-tree hit test in client-DIP coordinates, from `node` down.
    [[nodiscard]] static ui::Element* hit_test(ui::Element& node, ui::Point client_dips);

    HWND                                hwnd_;
    ui::Window*                         window_;
    mutable ui::Element*                element_; // the root's follows effective_root()
    std::shared_ptr<AutomationLifetime> lifetime_;
    std::uint64_t                       generation_; // window_->content_generation() as of construction
    bool                                is_root_;
    LONG                                refs_ = 1; // LONG for InterlockedIncrement/Decrement
};

// Raises UI Automation events for one window, which reports to it as its AutomationEvents.
class Win32AutomationEvents final : public ui::AutomationEvents
{
public:
    Win32AutomationEvents(HWND hwnd, ui::Window* window, std::shared_ptr<AutomationLifetime> lifetime) :
        hwnd_(hwnd),
        window_(window),
        lifetime_(std::move(lifetime))
    {
    }

    [[nodiscard]] bool listening() const override;
    void               focus_changed(ui::Element& node) override;
    void               property_changed(ui::Element&               node,
                                        ui::AutomationProperty     property,
                                        const ui::AutomationValue& before,
                                        const ui::AutomationValue& after) override;
    void               menu_opened(ui::Element& menu) override;
    void               menu_closed(ui::Element& menu) override;
    void               structure_changed(ui::Element& node) override;

private:
    [[nodiscard]] Microsoft::WRL::ComPtr<Win32AutomationProvider> provider_for(ui::Element& node) const;

    HWND                                hwnd_;
    ui::Window*                         window_;
    std::shared_ptr<AutomationLifetime> lifetime_;
};

} // namespace ui::win32

#endif // _WIN32
