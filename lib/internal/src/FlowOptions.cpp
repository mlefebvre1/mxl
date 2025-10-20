#include "mxl-internal/FlowOptions.hpp"
#include <cstdint>

namespace mxl::lib
{
    bool FlowOptions::hasHistoryDuration() const noexcept
    {
        return _impl.history_duration.get().has_value();
    }

    std::uint64_t FlowOptions::getHistoryDurationNs() const noexcept
    {
        return *_impl.history_duration.get();
    }

}
