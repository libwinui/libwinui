// SPDX-License-Identifier: MIT
#include "winui/render/ImageSource.hpp"

#include "winui/core/Scheduler.hpp"
#include "winui/core/Utf8Path.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <cctype>

namespace ui
{

namespace detail
{
struct ImageLoad
{
    std::atomic<ImageLoadState> state{ImageLoadState::Loading};
    // Written by the worker before `state` leaves Loading, read only after; never changed again.
    std::variant<std::monostate, Bitmap, AnimatedImage, SvgImage> content;
    std::string                                                   error;
    // when_loaded()'s callbacks, taken and run by the worker once `state` left Loading.
    std::mutex                    waiters_mutex;
    std::vector<Callback<void()>> waiters;
};
} // namespace detail

namespace
{
// Two workers: enough to keep the UI thread free while a page of pictures decodes, few enough
// not to compete with layout. Never destroyed: a decode may still be running when main() returns.
Scheduler& decoders()
{
    static Scheduler* const pool = new Scheduler{2};
    return *pool;
}

std::string lower_extension(const std::filesystem::path& path)
{
    std::string ext = path_to_utf8(path.extension());
    std::transform(ext.begin(),
                   ext.end(),
                   ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
} // namespace

ImageSource ImageSource::load(std::filesystem::path path)
{
    ImageSource source;
    source.load_ = std::make_shared<detail::ImageLoad>();
    (void)detail::submit(decoders(),
                         [load = source.load_, path = std::move(path)]
                         {
                             try
                             {
                                 const std::string ext = lower_extension(path);
                                 if (ext == ".svg")
                                 {
                                     load->content = SvgImage::read(path);
                                 }
                                 else if (ext == ".gif")
                                 {
                                     load->content = AnimatedImage::read(path);
                                 }
                                 else
                                 {
                                     Bitmap image = Bitmap::read(path);
                                     if (image.empty())
                                     {
                                         throw std::runtime_error{"cannot decode " + path_to_utf8(path)};
                                     }
                                     load->content = std::move(image);
                                 }
                                 load->state.store(ImageLoadState::Loaded, std::memory_order_release);
                             } catch (const std::exception& e)
                             {
                                 load->error = e.what();
                                 load->state.store(ImageLoadState::Failed, std::memory_order_release);
                             }
                             std::vector<Callback<void()>> waiters;
                             {
                                 const std::lock_guard lock{load->waiters_mutex};
                                 waiters.swap(load->waiters);
                             }
                             for (Callback<void()>& notify : waiters)
                             {
                                 notify();
                             }
                         });
    return source;
}

bool ImageSource::when_loaded(Callback<void()> notify) const
{
    if (!load_)
    {
        return false;
    }
    // The worker sets the state before it takes the waiters under this lock, so a callback added
    // while the state still says Loading is always run.
    const std::lock_guard lock{load_->waiters_mutex};
    if (load_->state.load(std::memory_order_acquire) != ImageLoadState::Loading)
    {
        return false;
    }
    load_->waiters.push_back(std::move(notify));
    return true;
}

ImageLoadState ImageSource::load_state() const
{
    return load_ ? load_->state.load(std::memory_order_acquire) : ImageLoadState::Loaded;
}

std::string ImageSource::load_error() const
{
    return load_state() == ImageLoadState::Failed ? load_->error : std::string{};
}

const ImageSource::Content& ImageSource::content() const
{
    if (load_)
    {
        static const Content nothing{};
        return load_->state.load(std::memory_order_acquire) == ImageLoadState::Loaded ? load_->content : nothing;
    }
    return content_;
}

} // namespace ui
