#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "han_graphics_small.h"
#include "src/draw/lv_image_decoder_private.h"

namespace {
std::vector<uint8_t> pixels(1280 * 720 * 3);
void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void Flush(lv_display_t* display, const lv_area_t* area, uint8_t* data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            auto i = (y * 1280 + x) * 3;
            pixels[i] = data[2];
            pixels[i + 1] = data[1];
            pixels[i + 2] = data[0];
            data += 4;
        }
    lv_display_flush_ready(display);
}
void Decode(const lv_image_dsc_t* src) {
    lv_image_decoder_dsc_t decoded{};
    lv_image_decoder_args_t args{};
    args.no_cache = true;
    Check(lv_image_decoder_open(&decoded, src, &args) == LV_RESULT_OK, "PNG decode failed");
    const bool valid = decoded.decoded && decoded.decoded->header.w > 0 &&
                       decoded.decoded->header.h > 0 && decoded.decoded->data;
    lv_image_decoder_close(&decoded);
    Check(valid, "PNG returned empty decoded buffer");
}
}  // namespace

int main(int argc, char** argv) {
    try {
        Check(argc == 3, "Pass screenshot directory and SD graphics directory");
        lv_init();
        Check(han_graphics_count() == 72, "72 small icons expected");
        Check(!han_graphics_find(nullptr) && !han_graphics_find("missing"), "safe missing ID");
        Check(!han_graphics_at(han_graphics_count()), "safe bounds");
        auto display = lv_display_create(1280, 720);
        std::vector<uint8_t> buffer(1280 * 720 * 4);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
        lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(),
                               LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, Flush);
        auto screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xfff9f0), 0);
        auto title = lv_label_create(screen);
        lv_label_set_text(
            title,
            "LVGL 9: actual PNG decoding + transparent compositing (96px weather / 48px controls)");
        lv_obj_set_pos(title, 24, 18);
        for (size_t i = 0; i < han_graphics_count(); ++i) {
            const auto entry = han_graphics_at(i);
            Check(han_graphics_find(entry->id) == entry->image, "ID lookup mismatch");
            Decode(entry->image);
            const int x = 24 + static_cast<int>(i % 12) * 104;
            const int y = 65 + static_cast<int>(i / 12) * 105;
            auto icon = lv_image_create(screen);
            lv_image_set_src(icon, entry->image);
            lv_obj_set_pos(icon, x, y);
        }
        lv_refr_now(display);
        // Check the renderer did not silently skip an image: each cell has non-background pixels.
        for (size_t i = 0; i < han_graphics_count(); ++i) {
            const int x0 = 24 + static_cast<int>(i % 12) * 104;
            const int y0 = 65 + static_cast<int>(i / 12) * 105;
            size_t ink = 0;
            for (int y = y0; y < y0 + 96; ++y)
                for (int x = x0; x < x0 + 96; ++x) {
                    const auto p = (y * 1280 + x) * 3;
                    if (pixels[p] != 255 || pixels[p + 1] != 249 || pixels[p + 2] != 240)
                        ++ink;
                }
            Check(ink >= 5, "rendered icon cell is empty");
        }
        std::filesystem::create_directories(argv[1]);
        std::ofstream file(std::filesystem::path(argv[1]) / "graphics-lvgl.ppm", std::ios::binary);
        file << "P6\n1280 720\n255\n";
        file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
        Check(file.good(), "screenshot write failed");
        file.close();
        size_t count = 0;
        // Also decode all SD sizes, including large artwork, with the same LVGL decoder.
        for (const auto& item : std::filesystem::recursive_directory_iterator(argv[2])) {
            if (item.path().extension() != ".png")
                continue;
            std::ifstream input(item.path(), std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            Check(!bytes.empty() && bytes.size() < 1024 * 1024, "SD PNG size");
            lv_image_dsc_t src{};
            src.header.magic = LV_IMAGE_HEADER_MAGIC;
            src.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
            src.data_size = static_cast<uint32_t>(bytes.size());
            src.data = bytes.data();
            Decode(&src);
            ++count;
        }
        Check(count == 252, "252 SD PNGs expected");
        std::cout << "PASS: 72 C descriptors decoded/rendered; 252 SD PNGs decoded.\n";
        lv_deinit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
