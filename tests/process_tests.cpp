#include "thumbnail/process_runner.h"

#include <windows.h>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace thumbnail;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

ProcessResult run(const std::filesystem::path& executable, std::initializer_list<std::wstring> arguments,
                  const ProcessLimits& limits = {}) {
    return run_limited_process(executable, std::vector<std::wstring>(arguments), limits);
}

void isolation_tests(const std::filesystem::path& helper) {
    const auto arguments = run(helper, {L"arguments", L"", L"한글 공백", L"a\\\"b", L"C:\\tail\\"});
    require(arguments.exit_code == 0 && !arguments.timed_out, "Windows argument quoting failed.");
    require(run(helper, {L"exit"}).exit_code == 37, "Nonzero worker exit was lost.");
    require(run(helper, {L"abnormal"}).exit_code == 0xc0000005, "Abnormal worker exit was lost.");
    const auto noisy = run(helper, {L"noisy"});
    require(noisy.exit_code == 0 && noisy.diagnostic.size() == 16 * 1024, "Diagnostic cap or pipe draining failed.");
    ProcessLimits short_limit;
    short_limit.timeout = std::chrono::milliseconds(100);
    const auto started = std::chrono::steady_clock::now();
    const auto hung = run(helper, {L"sleep"}, short_limit);
    require(hung.timed_out && hung.exit_code == ERROR_TIMEOUT, "Hung worker was not terminated.");
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(3), "Timeout took too long.");
    ProcessLimits small_memory;
    small_memory.memory_bytes = 64ULL * 1024 * 1024;
    const auto memory = run(helper, {L"memory"}, small_memory);
    require(memory.exit_code == 33 && !memory.timed_out, "Windows did not enforce committed memory limit.");
    require(memory.peak_committed_bytes <= small_memory.memory_bytes, "Worker exceeded memory limit.");
    DWORD before = 0, after = 0;
    require(GetProcessHandleCount(GetCurrentProcess(), &before), "Cannot count handles.");
    for (unsigned index = 0; index < 8; ++index) { require(run(helper, {L"exit"}).exit_code == 37, "Repeat worker failed."); }
    require(GetProcessHandleCount(GetCurrentProcess(), &after), "Cannot count handles.");
    require(after <= before + 1, "Worker process handles leaked.");
    std::cout << "PASS argument quoting, nonzero/abnormal exits, capped logs, timeout, memory limit, handle cleanup\n";
}

void model_workers(const std::filesystem::path& worker, const std::filesystem::path& samples,
                   const std::filesystem::path& temporary) {
    const char* files[]{"bearing_binary.stl", "bearing.obj", "bearing.3mf", "bearing_ap214.step"};
    for (const auto* name : files) {
        const auto input = temporary / (L"한글 입력 " + std::filesystem::path(name).wstring());
        const auto output = temporary / (std::filesystem::path(name).wstring() + L" 썸네일.png");
        std::filesystem::copy_file(samples / name, input);
        const auto result = run(worker, {input.wstring(), output.wstring(), L"--size", L"128"});
        require(result.exit_code == 0 && !result.timed_out, "Real model failed in isolated worker.");
        require(result.diagnostic.find("썸네일.png") != std::string::npos, "Unicode output diagnostic was lost.");
        require(std::filesystem::file_size(output) > 100, "Worker produced no PNG.");
        const auto size = std::filesystem::file_size(output);
        require(run(worker, {input.wstring(), output.wstring()}).exit_code != 0, "Worker overwrote existing output.");
        require(std::filesystem::file_size(output) == size, "Existing PNG changed.");
        std::cout << "PASS isolated " << name << ": peak commit " << result.peak_committed_bytes / (1024 * 1024) << " MiB\n";
    }
    const auto bad = temporary / "broken.step";
    std::ofstream(bad) << "not a STEP file";
    const auto absent = temporary / "must-not-exist.png";
    require(run(worker, {bad.wstring(), absent.wstring()}).exit_code != 0, "Invalid STEP did not fail.");
    require(!std::filesystem::exists(absent), "Invalid STEP produced an output.");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 3, "Usage: process-tests <samples> <test-output>");
        const auto directory = executable_path().parent_path();
        isolation_tests(directory / "process-fixture.exe");
        const auto temporary = std::filesystem::absolute(std::filesystem::path(argv[2]) /
            (L"작업자 검증 " + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count())));
        std::filesystem::create_directories(temporary);
        model_workers(directory / "thumbnail-worker.exe", argv[1], temporary);
        std::filesystem::remove_all(temporary);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
