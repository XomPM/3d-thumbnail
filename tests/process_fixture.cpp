#include <windows.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include "thumbnail/renderer.h"

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc < 2) { return 2; }
    if (argc == 5 && std::wstring(argv[3]) == L"--size") {
        std::ifstream input{std::filesystem::path(argv[1])};
        std::string action;
        input >> action;
        if (action == "alpha") {
            thumbnail::Image image{64,64,std::vector<std::uint8_t>(64*64*4)};
            for (unsigned y = 0; y < 64; ++y) {
                for (unsigned x = 0; x < 64; ++x) {
                    const auto offset = (y * 64 + x) * 4;
                    image.rgba[offset] = 200; image.rgba[offset+1] = 100; image.rgba[offset+2] = 50;
                    image.rgba[offset+3] = (x < 8 && y < 8) ? 0 : x < 32 ? 128 : 255;
                }
            }
            thumbnail::write_png(image, std::filesystem::path(argv[2]));
            return 0;
        }
        if (action == "sleep") { Sleep(10000); }
        if (action == "partial") { std::ofstream(std::filesystem::path(argv[2])) << "incomplete PNG"; }
        return action == "failure" ? 1 : 0;
    }
    const std::wstring mode(argv[1]);
    if (mode == L"sleep") { Sleep(10000); return 0; }
    if (mode == L"exit") { return 37; }
    if (mode == L"abnormal") { ExitProcess(0xc0000005); }
    if (mode == L"noisy") { std::cout << std::string(200000, 'x'); return 0; }
    if (mode == L"arguments") {
        return argc == 6 && std::wstring(argv[2]).empty() && std::wstring(argv[3]) == L"한글 공백" &&
            std::wstring(argv[4]) == L"a\\\"b" && std::wstring(argv[5]) == L"C:\\tail\\" ? 0 : 2;
    }
    if (mode == L"memory") {
        // The test job allows only 64 MiB; this commit must be refused by Windows.
        auto* allocation = VirtualAlloc(nullptr, 128ULL * 1024 * 1024, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!allocation) { return 33; }
        VirtualFree(allocation, 0, MEM_RELEASE);
        return 0;
    }
    return 2;
}
