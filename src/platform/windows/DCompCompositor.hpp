// SPDX-License-Identifier: MIT
#pragma once
#ifdef _WIN32

#include "../../render/Compositor.hpp"
#include "winui/render/RenderRecording.hpp"
#include "winui/ui/Animation.hpp"

#include <chrono>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace ui::win32
{

using Microsoft::WRL::ComPtr;

class DCompCompositor final : public detail::Compositor
{
public:
    // nullptr when D3D11 or DirectComposition is unavailable; the window then runs without one.
    static std::unique_ptr<DCompCompositor> create(HWND hwnd, UINT dpi);

    void sync_visual(VisualId id, const Rect& bounds, float corner_radius, const Color& color) override;
    void animate_opacity(VisualId id, float from, float to, Duration duration) override;
    void remove_visual(VisualId id) override;

    [[nodiscard]] bool supports_layers() override
    {
        return true;
    }
    void                   sync_layer(VisualId id, const detail::LayerPlacement& placement) override;
    void                   paint_layer(VisualId id, const RenderRecording& content) override;
    void                   animate_layer(VisualId id, const LayerAnimation& animation) override;
    void                   stop_layer_animations(VisualId id) override;
    void                   remove_layer(VisualId id) override;
    [[nodiscard]] Duration now() const override;

    // The overlay geometry is authored in DIPs like everything else in the framework;
    // this converts to the physical pixels DirectComposition actually works in. The layers are
    // drawn again at the new scale.
    void set_dpi(UINT dpi);

    // The visual every layer hangs under: the root's first child, so the overlays (added on top of
    // it later) stay above the layers. Content that belongs under both, such as the window's own
    // frame as a visual, goes below it.
    [[nodiscard]] IDCompositionVisual2* layers_root() const
    {
        return layers_root_.Get();
    }

private:
    // A layer is three visuals deep, so each clip is in a space that its animations leave alone:
    //   clip_visual     window pixels, clipped to the placement's clip
    //   frame_visual    offset to the surface's top-left pixel, clipped by the animated clip
    //   content_visual  the surface, under the animated scale, rotation, translation and opacity
    struct Layer
    {
        struct Running
        {
            LayerAnimation                        animation;
            std::chrono::steady_clock::time_point started;
        };

        ComPtr<IDCompositionVisual2>            clip_visual;
        ComPtr<IDCompositionVisual2>            frame_visual;
        ComPtr<IDCompositionVisual3>            content_visual;
        ComPtr<IDCompositionRectangleClip>      own_clip;
        ComPtr<IDCompositionScaleTransform>     scale;
        ComPtr<IDCompositionRotateTransform>    rotate;
        ComPtr<IDCompositionTranslateTransform> translate;
        ComPtr<IDCompositionSurface>            surface;
        RenderRecording                         content;
        detail::LayerPlacement                  placement;
        // The surface: its top-left pixel in the window, its size, and where the layer's (0,0)
        // falls inside it, in pixels.
        int                  origin_x = 0, origin_y = 0;
        int                  width_px = 0, height_px = 0;
        float                inset_x = 0.0f, inset_y = 0.0f;
        UINT                 painted_dpi = 0;
        std::vector<Running> running; // one per property
    };
    // IDCompositionVisual3 is the first generation with SetOpacity.
    struct Overlay
    {
        ComPtr<IDCompositionVisual3> visual;
        ComPtr<IDCompositionSurface> surface;
        int                          width_px  = -1;
        int                          height_px = -1;
        float                        radius_px = -1.0f;
        Color                        color{};
    };

    DCompCompositor() = default;

    [[nodiscard]] float to_px(float dips) const
    {
        return dips * static_cast<float>(dpi_) / 96.0f;
    }

    void paint_overlay(Overlay& overlay, int width_px, int height_px, float radius_px, const Color& color);

    // Works out the layer's surface for its placement at the current DPI, moves its visuals there,
    // and draws it again when the surface changed. Returns whether it did.
    bool place_layer(Layer& layer);
    void draw_layer(Layer& layer);
    // Hands `animation` to the visuals, in the surface's pixels.
    void run_animation(Layer& layer, const LayerAnimation& animation);
    // After the surface moved inside the layer: every running animation again, from where it is.
    void restart_animations(Layer& layer);

    ComPtr<ID3D11Device>                  d3d_device_;
    ComPtr<ID2D1Device>                   d2d_device_;
    ComPtr<IDCompositionDesktopDevice>    device_;
    ComPtr<IDCompositionTarget>           target_;
    ComPtr<IDCompositionVisual2>          root_;
    ComPtr<IDCompositionVisual2>          layers_root_;
    ComPtr<IDWriteFactory>                dwrite_; // made by the first layer drawn
    UINT                                  dpi_ = 96;
    std::unordered_map<VisualId, Overlay> overlays_;
    std::unordered_map<VisualId, Layer>   layers_;
};

// What a window hands its tree: a DCompCompositor made on the first animation. Creating one costs
// a hardware D3D11 device and DirectComposition, over 100 ms, before a window that may never
// animate anything could show its first frame. Until then the overlays the tree describes are only
// remembered (they start invisible), and replayed when the device comes.
class LazyCompositor final : public detail::Compositor
{
public:
    LazyCompositor(HWND hwnd, UINT dpi) : hwnd_(hwnd), dpi_(dpi)
    {
    }

    void sync_visual(VisualId id, const Rect& bounds, float corner_radius, const Color& color) override;
    void animate_opacity(VisualId id, float from, float to, Duration duration) override;
    void remove_visual(VisualId id) override;

    // Makes the device for the first layer, and says no when there is none to make.
    [[nodiscard]] bool supports_layers() override
    {
        return device() != nullptr;
    }
    void                   sync_layer(VisualId id, const detail::LayerPlacement& placement) override;
    void                   paint_layer(VisualId id, const RenderRecording& content) override;
    void                   animate_layer(VisualId id, const LayerAnimation& animation) override;
    void                   stop_layer_animations(VisualId id) override;
    void                   remove_layer(VisualId id) override;
    [[nodiscard]] Duration now() const override;

    void set_dpi(UINT dpi);

    // Makes the device now, for an idle moment before the first animation would have to wait for it.
    void prepare()
    {
        (void)device();
    }

private:
    struct Pending
    {
        Rect  bounds{};
        float corner_radius = 0.0f;
        Color color{};
    };

    // The real compositor, made (once) and caught up with the overlays described so far; null
    // when DirectComposition is unavailable, and the animations are then dropped.
    DCompCompositor* device();

    HWND                                  hwnd_;
    UINT                                  dpi_;
    bool                                  tried_ = false;
    std::unique_ptr<DCompCompositor>      real_;
    std::unordered_map<VisualId, Pending> pending_;
};

} // namespace ui::win32

#endif // _WIN32
