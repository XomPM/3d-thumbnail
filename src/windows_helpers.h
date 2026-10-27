#pragma once

#include <windows.h>
#include <objbase.h>
#include <sstream>
#include <stdexcept>

namespace thumbnail {

inline void check_hresult(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ").";
        throw std::runtime_error(message.str());
    }
}

class ComApartment {
public:
    explicit ComApartment(DWORD model = COINIT_MULTITHREADED) : result_(CoInitializeEx(nullptr, model)) {
        if (result_ != RPC_E_CHANGED_MODE) {
            check_hresult(result_, "COM initialization");
        }
    }
    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
private:
    HRESULT result_;
};

} // namespace thumbnail
