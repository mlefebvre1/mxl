#pragma once

#include <rfl.hpp>

namespace mxl::lib
{
    struct FlowOptions
    {
    public:
        struct Impl
        {
            rfl::Rename<"urn:x-mxl:option:history_duration/v1.0", std::optional<std::uint64_t>> history_duration;
        };

    public: // reflection
        using ReflectionType = Impl;

        FlowOptions(Impl impl)
            : _impl(std::move(impl))
        {}

        [[nodiscard]]
        ReflectionType const& reflection() const
        {
            return _impl;
        }

    public:
        [[nodiscard]]
        bool hasHistoryDuration() const noexcept;

        [[nodiscard]]
        std::uint64_t getHistoryDurationNs() const noexcept;

    private:
        Impl _impl;
    };

}
