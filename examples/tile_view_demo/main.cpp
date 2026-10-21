// SPDX-License-Identifier: MIT
// A board of pictures in a TileView, put in order by hand: check several and drag them
// together, carry them with Ctrl and the arrow keys, size the tiles with Ctrl and the wheel,
// and drop image files from Explorer where the gap opens. The pictures start out painted in
// code, so the demo needs no files.
#include <winui/winui.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace
{

/// The tile widths Ctrl+wheel and the slider step through, in DIPs.
constexpr std::array<float, 6> TileSizes{96.0f, 128.0f, 160.0f, 200.0f, 256.0f, 320.0f};
constexpr int                  DefaultSize = 2;

/// What a tile shows that changes after it was built: the picture once decoded, and its place
/// in the order. Shared by the item and its tile, so a move or a finished decode updates the
/// tile in place.
struct TileState
{
    ui::Property<ui::ImageSource> image;
    ui::Property<std::string>     position;
};

struct Tile
{
    std::size_t                id = 0;
    std::string                name;
    std::shared_ptr<TileState> state = std::make_shared<TileState>();

    friend bool operator==(const Tile& a, const Tile& b)
    {
        return a.id == b.id;
    }
};

/// A landscape in the colors of `hue` (0..1): sky, sun and hills.
ui::Bitmap paint_picture(float hue, int width, int height)
{
    ui::Bitmap  image{width, height};
    const float sun_x = static_cast<float>(width) * (0.55f + 0.3f * hue);
    const float sun_y = static_cast<float>(height) * 0.3f;
    const float sun_r = static_cast<float>(height) * 0.13f;
    for (int y = 0; y < height; ++y)
    {
        const float t = static_cast<float>(y) / static_cast<float>(height);
        for (int x = 0; x < width; ++x)
        {
            const float u    = static_cast<float>(x) / static_cast<float>(width);
            const float hill = 0.62f + 0.12f * std::sin(u * 7.0f + hue * 11.0f) + 0.05f * std::sin(u * 19.0f);
            const float dx   = static_cast<float>(x) - sun_x;
            const float dy   = static_cast<float>(y) - sun_y;
            ui::Color   c{0.25f + 0.5f * hue * t, 0.45f + 0.3f * (1.0f - t), 0.9f - 0.3f * hue * t};
            if (dx * dx + dy * dy < sun_r * sun_r)
            {
                c = ui::Color::from_rgb(255, 214, 102);
            }
            if (t > hill)
            {
                c = ui::Color{0.15f + 0.2f * hue, 0.45f + 0.45f * (t - hill), 0.25f};
            }
            image.set(x, y, c);
        }
    }
    return image;
}

bool is_picture(const std::filesystem::path& path)
{
    std::string ext = ui::path_to_utf8(path.extension());
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif" || ext == ".tif" ||
           ext == ".tiff" || ext == ".webp" || ext == ".heic";
}

std::vector<std::filesystem::path> pictures_in(const ui::DataPackage& data)
{
    std::vector<std::filesystem::path> out;
    std::ranges::copy_if(data.files, std::back_inserter(out), is_picture);
    return out;
}

class Board
{
public:
    void attach(ui::Window& window)
    {
        window_ = &window;
        add_samples(12);
    }

    /// Joins the decoder threads while the window can still take their posts.
    void shutdown()
    {
        decoders_.clear();
    }

    void add_samples(std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            Tile tile         = make_tile(std::format("Sample {}", ++samples_));
            tile.state->image = ui::ImageSource(paint_picture(static_cast<float>(samples_ % 13) / 13.0f, 240, 160));
            tiles.push_back(std::move(tile));
        }
        renumber();
    }

    void pick_and_add()
    {
        const std::vector<std::filesystem::path>
            files = ui::pick_files(*window_,
                                   {.title      = "Add pictures",
                                    .file_types = {
                                        ui::FileType{.name       = "Pictures",
                                                     .extensions = {".png", ".jpg", ".jpeg", ".bmp", ".gif"}}}});
        add_files(files, tiles.size());
    }

    /// Puts a tile for each file at `at`, then decodes the thumbnails on a background thread.
    void add_files(const std::vector<std::filesystem::path>& files, std::size_t at)
    {
        std::vector<std::pair<std::shared_ptr<TileState>, std::filesystem::path>> jobs;
        at = std::min(at, tiles.size());
        for (const std::filesystem::path& file : files)
        {
            Tile tile = make_tile(ui::path_to_utf8(file.filename()));
            jobs.emplace_back(tile.state, file);
            tiles.insert(at++, std::move(tile));
        }
        renumber();
        if (jobs.empty())
        {
            return;
        }
        decoders_.emplace_back(
            [this, jobs = std::move(jobs)](std::stop_token stop)
            {
                for (const auto& [state, file] : jobs)
                {
                    if (stop.stop_requested())
                    {
                        return;
                    }
                    ui::Bitmap thumbnail;
                    try
                    {
                        thumbnail = ui::Bitmap::read_thumbnail(file, 480);
                    } catch (...)
                    {
                        continue;
                    }
                    window_->post([state, thumbnail = std::move(thumbnail)]() mutable
                                  { state->image = ui::ImageSource(std::move(thumbnail)); });
                }
            });
    }

    void remove_selected()
    {
        std::vector<std::size_t> selected = grid->selected_items();
        for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        {
            tiles.erase(*it);
        }
        renumber();
    }

    void select_all()
    {
        grid->select_all();
    }

    void select_none()
    {
        grid->deselect_all();
    }

    void zoom_by(int steps)
    {
        size_index = std::clamp(std::lround(size_index.get()) + steps, 0L, static_cast<long>(TileSizes.size() - 1));
    }

    /// Ctrl and an arrow key: the list moves its focus as for any Ctrl+arrow, and the tile, or
    /// every checked tile when the focused one is checked, follows by the same distance.
    ui::Handling key_down(const ui::KeyEvent& e)
    {
        if (e.key == ui::Key::Delete && !grid->selected_items().empty())
        {
            remove_selected();
            return ui::Handled;
        }
        const bool arrow = e.key == ui::Key::ArrowLeft || e.key == ui::Key::ArrowRight || e.key == ui::Key::ArrowUp ||
                           e.key == ui::Key::ArrowDown;
        const std::optional<std::size_t> from = grid->current_item();
        if (!arrow || !e.modifiers.ctrl || !from)
        {
            return ui::NotHandled;
        }
        window_->post(
            [this, from = *from]
            {
                const std::optional<std::size_t> to = grid->current_item();
                if (!to || *to == from)
                {
                    return;
                }
                const std::vector<std::size_t> block = grid->is_selected(from) ? grid->selected_items()
                                                                               : std::vector<std::size_t>{from};
                const auto           wanted = static_cast<std::ptrdiff_t>(*to) - static_cast<std::ptrdiff_t>(from);
                const std::ptrdiff_t shift  = std::clamp(wanted,
                                                         -static_cast<std::ptrdiff_t>(block.front()),
                                                         static_cast<std::ptrdiff_t>(tiles.size() - 1 - block.back()));
                // The tile nearest the direction of travel moves first, so each lands on a
                // place the others have left and the gaps between them stay.
                const auto step = [&](std::size_t i)
                { tiles.move(i, static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) + shift)); };
                if (shift > 0)
                {
                    std::for_each(block.rbegin(), block.rend(), step);
                }
                else
                {
                    std::for_each(block.begin(), block.end(), step);
                }
                grid->set_current_item(static_cast<std::size_t>(static_cast<std::ptrdiff_t>(from) + shift));
                renumber();
            });
        return ui::NotHandled;
    }

    /// The number in each tile's corner, and the status line.
    void renumber()
    {
        for (std::size_t i = 0; i < tiles.size(); ++i)
        {
            tiles[i].state->position = std::to_string(i + 1);
        }
        update_status();
    }

    void update_status()
    {
        const std::size_t checked = grid != nullptr ? grid->selected_items().size() : 0;
        status                    = checked > 0 ? std::format("{} pictures  ·  {} selected", tiles.size(), checked)
                                                : std::format("{} pictures", tiles.size());
        has_selection             = checked > 0;
    }

    ui::Spec build_tile(Tile& tile)
    {
        const auto width = [](double index)
        { return std::optional<float>{TileSizes[static_cast<std::size_t>(std::lround(index))]}; };
        const auto height = [](double index)
        { return std::optional<float>{TileSizes[static_cast<std::size_t>(std::lround(index))] * 2.0f / 3.0f}; };

        ui::Spec badge = ui::Border{
            .background           = ui::theme_colors::Accent,
            .corner_radius        = 10.0f,
            .padding              = ui::Edges{.left = 7.0f, .top = 1.0f, .right = 7.0f, .bottom = 2.0f},
            .margin               = ui::Edges{.left = 6.0f, .top = 6.0f},
            .horizontal_alignment = ui::Alignment::Leading,
            .vertical_alignment   = ui::Alignment::Leading,
            .content              = ui::Text{.text       = ui::bind(tile.state->position),
                                             .role       = ui::TextRole::Caption,
                                             .foreground = ui::theme_colors::TextOnAccent},
        };
        ui::Spec picture = ui::Grid{
            .width  = ui::bind(size_index, width),
            .height = ui::bind(size_index, height),
            .content =
                {
                    ui::Cell{.content = ui::Border{.background = ui::theme_colors::ControlFill, .corner_radius = 4.0f}},
                    ui::Cell{.content = ui::Image{.source = ui::bind(tile.state->image), .stretch = ui::Stretch::UniformToFill}},
                    ui::Cell{.content = std::move(badge)},
                },
        };
        return ui::VerticalStack{
            .spacing       = 6.0f,
            .padding       = ui::Thickness{8.0f},
            .accessibility = {.name = tile.name},
            .content =
                {
                    // The picture follows the tile's rounded corners.
                    ui::Border{.corner_radius = 6.0f, .clip_content = true, .content = std::move(picture)},
                    ui::Text{.text     = tile.name,
                             .trimming = ui::TextTrimming::CharacterEllipsis,
                             .width    = ui::bind(size_index, width)},
                },
        };
    }

    ui::ObservableVector<Tile>      tiles;
    ui::Control<ui::TileView<Tile>> grid;
    ui::Property<double>            size_index{static_cast<double>(DefaultSize)};
    ui::Property<std::string>       status;
    ui::Property<bool>              has_selection{false};

private:
    Tile make_tile(std::string name)
    {
        return Tile{.id = next_id_++, .name = std::move(name)};
    }

    ui::Window*               window_  = nullptr;
    std::size_t               next_id_ = 0;
    std::size_t               samples_ = 0;
    std::vector<std::jthread> decoders_;
};

ui::Spec command_bar(Board& board)
{
    return ui::CommandBar{
        .default_text_position = ui::CommandBarDefaultTextPosition::Right,
        .margin                = ui::Edges{.left = 8.0f, .top = 4.0f, .right = 8.0f, .bottom = 4.0f},
        .content               = ui::Text{.text               = "Board",
                                          .role               = ui::TextRole::Subtitle,
                                          .margin             = ui::Edges{.left = 12.0f},
                                          .vertical_alignment = ui::Alignment::Center},
        .primary =
            {
                ui::AppBarButton{.text     = "Add pictures",
                                 .icon     = ui::Symbol::Add,
                                 .shortcut = ui::KeyCombo{ui::Key::O, ui::Ctrl},
                                 .on_click = [&board] { board.pick_and_add(); }},
                ui::AppBarButton{.text     = "Add samples",
                                 .icon     = ui::Symbol::Pictures,
                                 .on_click = [&board] { board.add_samples(6); }},
                ui::AppBarSeparator{},
                ui::AppBarButton{.text     = "Remove",
                                 .icon     = ui::Symbol::Delete,
                                 .enabled  = ui::bind(board.has_selection),
                                 .on_click = [&board] { board.remove_selected(); }},
            },
        .secondary =
            {
                ui::AppBarButton{.text     = "Select all",
                                 .icon     = ui::Symbol::SelectAll,
                                 .shortcut = ui::KeyCombo{ui::Key::A, ui::Ctrl},
                                 .on_click = [&board] { board.select_all(); }},
                ui::AppBarButton{.text     = "Select none",
                                 .icon     = ui::Symbol::ClearSelection,
                                 .shortcut = ui::KeyCombo{ui::Key::A, ui::Ctrl + ui::Shift},
                                 .on_click = [&board] { board.select_none(); }},
                ui::AppBarSeparator{},
                ui::AppBarButton{.text     = "Larger tiles",
                                 .icon     = ui::Symbol::ZoomIn,
                                 .shortcut = ui::KeyCombo{ui::Key::Add, ui::Ctrl},
                                 .on_click = [&board] { board.zoom_by(1); }},
                ui::AppBarButton{.text     = "Smaller tiles",
                                 .icon     = ui::Symbol::ZoomOut,
                                 .shortcut = ui::KeyCombo{ui::Key::Subtract, ui::Ctrl},
                                 .on_click = [&board] { board.zoom_by(-1); }},
            },
    };
}

ui::Spec tile_grid(Board& board)
{
    return ui::TileView<Tile>{
        .items                = board.tiles,
        .selection_mode       = ui::SelectionMode::Multiple,
        .can_drag_items       = true,
        .can_reorder_items    = true,
        .allow_drop           = true,
        .margin               = ui::Edges{.left = 12.0f, .right = 12.0f},
        .visible              = ui::bind(board.tiles.count(), [](std::size_t count) { return count > 0; }),
        .accessibility        = {.name = "Pictures"},
        .on_selection_changed = [&board](const auto&) { board.update_status(); },
        .on_items_reordered   = [&board](const ui::ItemsReorderedArgs&) { board.renumber(); },
        .on_key_down          = [&board](const ui::KeyEvent& e) { return board.key_down(e); },
        .on_wheel =
            [&board](const ui::PointerEvent& e)
        {
            // Ctrl and the wheel sizes the tiles; a turn up (negative) makes them larger.
            if (!e.modifiers.ctrl || e.horizontal_wheel || e.wheel_delta == 0.0f)
            {
                return ui::NotHandled;
            }
            board.zoom_by(e.wheel_delta < 0.0f ? 1 : -1);
            return ui::Handled;
        },
        .control = board.grid,
        .item    = [&board](Tile& tile) { return board.build_tile(tile); },
    };
}

ui::Spec empty_state(Board& board)
{
    return ui::VerticalStack{
        .spacing              = 8.0f,
        .horizontal_alignment = ui::Alignment::Center,
        .vertical_alignment   = ui::Alignment::Center,
        .visible              = ui::bind(board.tiles.count(), [](std::size_t count) { return count == 0; }),
        .content =
            {
                ui::Text{.text                 = "The board is empty",
                         .role                 = ui::TextRole::Subtitle,
                         .horizontal_alignment = ui::Alignment::Center},
                ui::Text{.text                 = "Add pictures, or drop image files here from Explorer.",
                         .foreground           = ui::theme_colors::TextSecondary,
                         .horizontal_alignment = ui::Alignment::Center},
            },
    };
}

ui::Spec status_bar(Board& board)
{
    return ui::Grid{
        .columns        = {ui::GridLength::fraction(), ui::GridLength::auto_()},
        .column_spacing = 12.0f,
        .margin         = ui::Edges{.left = 20.0f, .top = 4.0f, .right = 16.0f, .bottom = 8.0f},
        .content =
            {
                ui::Cell{.column  = 0,
                         .content = ui::Text{.text               = ui::bind(board.status),
                                             .role               = ui::TextRole::Caption,
                                             .vertical_alignment = ui::Alignment::Center}},
                ui::Cell{.column  = 1,
                         .content = ui::Slider{.value              = ui::bind(board.size_index),
                                               .min                = 0.0,
                                               .max                = static_cast<double>(TileSizes.size() - 1),
                                               .step               = 1.0,
                                               .tick_frequency     = 1.0,
                                               .tick_placement     = ui::SliderTickPlacement::Outside,
                                               .min_width          = 160.0f,
                                               .vertical_alignment = ui::Alignment::Center,
                                               .accessibility      = {.name = "Tile size"}}},
            },
    };
}

} // namespace

int main()
{
    Board board;

    ui::Window window{{.title = "TileView demo", .size = {1000.0f, 720.0f}, .backdrop = ui::Backdrop::Mica}};

    ui::Spec     bar    = command_bar(board);
    ui::Spec     grid   = tile_grid(board);
    ui::Spec     empty  = empty_state(board);
    ui::Spec     status = status_bar(board);
    ui::Element* root   = window.set_content(ui::Grid{
        .rows = {ui::GridLength::auto_(), ui::GridLength::fraction(), ui::GridLength::auto_()},
        .content =
            {
                ui::Cell{.row = 0, .content = std::move(bar)},
                ui::Cell{.row = 1, .content = std::move(grid)},
                ui::Cell{.row = 1, .content = std::move(empty)},
                ui::Cell{.row = 2, .content = std::move(status)},
            },
    });

    // Image files from Explorer: dropped on the board they go where the gap opens, anywhere
    // else at the end.
    const auto accept = [](const ui::DragEventArgs& e)
    {
        const std::size_t count = pictures_in(e.data).size();
        if (count == 0)
        {
            return ui::DragResponse{};
        }
        return ui::DragResponse{.operation = ui::DataPackageOperation::Copy,
                                .caption = count == 1 ? std::string{"Add 1 picture"} : std::format("Add {} pictures", count)};
    };
    board.grid->on_drag_over(accept);
    board.grid->on_drop([&board](const ui::DragEventArgs& e)
                        { board.add_files(pictures_in(e.data), board.grid->insertion_index_at(e.position)); });
    root->set_allow_drop(true);
    root->on_drag_over(accept);
    root->on_drop([&board](const ui::DragEventArgs& e) { board.add_files(pictures_in(e.data), board.tiles.size()); });

    board.attach(window);
    window.on_closed([&board] { board.shutdown(); });
    return ui::run(window);
}
