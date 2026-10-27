#pragma once
#include "thumbnail/renderer.h"
#include <span>

namespace thumbnail {
// Reads the package's stored preview only; does not validate or repair its geometry.
Image read_3mf_preview(std::span<const std::byte> bytes, unsigned size);
}
