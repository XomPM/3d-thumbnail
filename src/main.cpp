#include "thumbnail/renderer.h"
#include "thumbnail/model_reader.h"
#include "thumbnail/package_preview.h"
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::wstring lowercase_extension(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return extension;
}

void show_usage() {
    std::wcout << L"3D Thumbnail worker 1.0.1\n"
               << L"Usage: thumbnail-cli INPUT OUTPUT.png [--size 64..1024] [--background #RRGGBB] [--model #RRGGBB] [--background-transparency 0..100]\n"
               << L"Formats: STL, STEP/STP, OBJ, 3MF, GLB/glTF, PLY, IGES/IGS, FBX. Default size 256.\n"
               << L"Output must not already exist. Run thumbnail-cli to enforce worker resource limits.\n";
}

unsigned parse_size(const std::wstring& text) {
    if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring::npos) {
        throw std::runtime_error("Size must be a whole number from 64 to 1024.");
    }
    const auto value = std::stoul(text);
    if (value < 64 || value > 1024) {
        throw std::runtime_error("Size must be a whole number from 64 to 1024.");
    }
    return static_cast<unsigned>(value);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc == 2 && std::wstring(argv[1]) == L"--help") {
        show_usage();
        return 0;
    }
    if (argc < 3 || argc > 11 || argc % 2 != 1) {
        show_usage();
        return 2;
    }
    try {
        const auto started = std::chrono::steady_clock::now();
        const std::filesystem::path input(argv[1]);
        const std::filesystem::path output(argv[2]);
        if (lowercase_extension(output) != L".png") {
            throw std::runtime_error("Output filename must have a .png extension.");
        }
        unsigned size = 256;
        auto appearance = thumbnail::load_appearance();
        for (int index = 3; index < argc; index += 2) {
            const std::wstring option(argv[index]);
            if (option == L"--size") { size = parse_size(argv[index + 1]); }
            else if (option == L"--background") { appearance.background = thumbnail::parse_color(argv[index + 1]); }
            else if (option == L"--model") { appearance.model = thumbnail::parse_color(argv[index + 1]); }
            else if (option == L"--background-transparency") { appearance.background_transparency = thumbnail::parse_transparency(argv[index + 1]); }
            else { throw std::runtime_error("Unknown option; use --size, --background, --model or --background-transparency."); }
        }
        if (std::filesystem::exists(output)) {
            throw std::runtime_error("Output already exists. Choose a new filename.");
        }
        thumbnail::Image image;
        std::size_t triangle_count = 0;
        try {
            const auto mesh = thumbnail::read_model(input);
            triangle_count = mesh.triangles.size();
            image = thumbnail::render_thumbnail(mesh, size, appearance);
        } catch (const std::runtime_error& geometry_error) {
            if (lowercase_extension(input) != L".3mf") { throw; }
            try { image = thumbnail::read_3mf_preview(thumbnail::read_model_bytes(input), size); }
            catch (const std::exception& preview_error) {
                throw std::runtime_error(std::string(geometry_error.what()) + " Stored preview unavailable: " + preview_error.what());
            }
            std::cout << "Using stored 3MF preview; original colors/background retained. Geometry was not validated.\n"
                      << "Direct rendering unavailable: " << geometry_error.what() << '\n';
        }
        thumbnail::write_png(image, output);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto output_utf8 = output.u8string();
        std::cout << "Saved " << std::string(output_utf8.begin(), output_utf8.end()) << '\n'
                  << triangle_count << " rendered triangles, " << size << 'x' << size
                  << ", " << elapsed << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
