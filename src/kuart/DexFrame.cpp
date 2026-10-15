#include "kudroid/kuart/DexFrame.h"

namespace kudroid {
namespace kuart {

void DexFrame::LoadArguments(const DexValue* args, size_t count, const char* shorty,
                            bool is_static) {
    uint32_t reg = FirstArgRegister();
    size_t arg_index = 0;

    if (!is_static) {
        if (count > 0) {
            Set(reg, args[0]);
            ++arg_index;
        }
        ++reg;
    }

    // shorty[0] is the return type; long/double take two slots.
    if (shorty != nullptr) {
        for (const char* p = shorty + 1; *p != '\0' && arg_index < count; ++p) {
            Set(reg, args[arg_index]);
            ++arg_index;
            reg += (*p == 'J' || *p == 'D') ? 2 : 1;
        }
    }
}

}  // namespace kuart
}  // namespace kudroid
