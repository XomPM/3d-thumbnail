#include "thumbnail/process_runner.h"

#include <iostream>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--help") {
        std::cout << "3D Thumbnail 0.5.1\n"
                  << "Usage: thumbnail-cli INPUT OUTPUT.png [--size 64..1024] [--background #RRGGBB] [--model #RRGGBB]\n"
                  << "Formats: STL, STEP/STP, OBJ, 3MF, GLB/glTF, PLY, IGES/IGS, FBX. Default size 256.\n"
                  << "Worker limits: 15 seconds, 1 GiB committed memory, one process.\n"
                  << "Output must not already exist. Explorer integration requires separate registration; see docs/shell-integration.md.\n";
        return 0;
    }
    if (argc < 3 || argc > 9 || argc % 2 != 1) {
        std::cerr << "Usage: thumbnail-cli INPUT OUTPUT.png [--size 64..1024] [--background #RRGGBB] [--model #RRGGBB]\n";
        return 2;
    }
    try {
        std::vector<std::wstring> arguments{std::filesystem::absolute(argv[1]).wstring(),
                                            std::filesystem::absolute(argv[2]).wstring()};
        for (int index = 3; index < argc; ++index) { arguments.emplace_back(argv[index]); }
        const auto worker = thumbnail::executable_path().parent_path() / L"thumbnail-worker.exe";
        const auto result = thumbnail::run_limited_process(worker, arguments);
        std::cout << result.diagnostic;
        if (result.timed_out) {
            std::cerr << "Error: thumbnail worker exceeded the 15-second limit.\n";
            return 3;
        }
        if (result.exit_code != 0) {
            std::cerr << "Worker failed (exit code " << result.exit_code << ").\n";
            return 1;
        }
        std::cout << "Worker peak committed memory: " << result.peak_committed_bytes / (1024 * 1024) << " MiB\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
