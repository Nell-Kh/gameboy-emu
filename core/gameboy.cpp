#include "core/gameboy.h"

namespace core {

std::string_view GameBoy::version() noexcept {
    return GB_VERSION;
}

}  // namespace core
