// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "Win32MessageWindow.hpp"

#include <memory>
#include <utility>

namespace ui::win32
{

namespace
{
constexpr UINT           RunPosted        = WM_APP + 1;
constexpr const wchar_t* MessageClassName = L"libwinui.MessageWindow";
} // namespace

MessageWindow& MessageWindow::get()
{
    static MessageWindow window;
    return window;
}

const wchar_t* MessageWindow::class_name()
{
    return MessageClassName;
}

MessageWindow::MessageWindow()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW     cls{};
    cls.cbSize        = sizeof(cls);
    cls.lpfnWndProc   = &MessageWindow::proc;
    cls.hInstance     = instance;
    cls.lpszClassName = MessageClassName;
    RegisterClassExW(&cls);
    hwnd_ = CreateWindowExW(0, MessageClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (hwnd_ != nullptr)
    {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        // A second instance running elevated, or not, still reaches it.
        ChangeWindowMessageFilterEx(hwnd_, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    }
}

void MessageWindow::post(Callback<void()> work) const
{
    if (hwnd_ == nullptr || !work)
    {
        return;
    }
    auto boxed = std::make_unique<Callback<void()>>(std::move(work));
    if (PostMessageW(hwnd_, RunPosted, 0, reinterpret_cast<LPARAM>(boxed.get())))
    {
        (void)boxed.release(); // the window's thread owns it now
    }
}

void MessageWindow::on_copy_data(Callback<bool(const COPYDATASTRUCT&)> handler)
{
    copy_data_ = std::move(handler);
}

LRESULT CALLBACK MessageWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* self = reinterpret_cast<MessageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
        case RunPosted:
        {
            const std::unique_ptr<Callback<void()>> work{reinterpret_cast<Callback<void()>*>(lp)};
            (*work)();
            return 0;
        }
        case WM_COPYDATA:
            if (self != nullptr && self->copy_data_ && lp != 0)
            {
                return self->copy_data_(*reinterpret_cast<const COPYDATASTRUCT*>(lp)) ? TRUE : FALSE;
            }
            return FALSE;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace ui::win32

#endif // _WIN32
