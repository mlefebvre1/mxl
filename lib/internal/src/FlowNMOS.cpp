#include <mxl-internal/FlowNMOS.hpp>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <rfl.hpp>
#include <uuid.h>
#include <fmt/color.h>
#include <rfl/json.hpp>
#include <rfl/json/write.hpp>
#include <rfl/TaggedUnion.hpp>
#include <rfl/visit.hpp>
#include "mxl/dataformat.h"
#include "mxl/rational.h"

namespace mxl::lib
{
    // Allows us to use the visitor pattern with lambdas
    template<class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };

    mxlRational Rational::toMxl() const noexcept
    {
        return {.numerator = numerator, .denominator = denominator};
    }

    Rational Rational::fromMxl(mxlRational mxl) noexcept
    {
        return {.numerator = mxl.numerator, .denominator = mxl.denominator};
    }

    bool Rational::operator==(Rational const& rhs) const noexcept
    {
        return numerator == rhs.numerator && denominator == rhs.denominator;
    }

    Rational::Rfl Rational::Rfl::from_class(Rational const& r) noexcept
    {
        return Rational::Rfl{.numerator = r.numerator, .denominator = r.denominator};
    }

    Rational Rational::Rfl::to_class() const
    {
        auto result = Rational{.numerator = 0, .denominator = 1};

        result.numerator = numerator.get();
        if (auto denom = denominator.get(); denom)
        {
            result.denominator = *denom;
        }

        // Normalize the rational. We should realistically only see x/1 or x/1001 here.
        auto g = std::gcd(result.numerator, result.denominator);
        if (g != 0)
        {
            result.numerator /= g;
            result.denominator /= g;
        }

        return result;
    }

    std::string_view NMOSCommonFlow::getDescription() const noexcept
    {
        return description.get();
    }

    uuids::uuid NMOSCommonFlow::getId() const
    {
        return *uuids::uuid::from_string(id.get().value());
    }

    std::string_view NMOSCommonFlow::getLabel() const noexcept
    {
        return label.get();
    }

    std::string_view NMOSCommonFlow::getMediaType() const noexcept
    {
        return mediaType.get();
    }

    std::vector<std::string> const& NMOSCommonFlow::getGroupHints() const noexcept
    {
        return tags.get().groupHints.get();
    }

    void NMOSCommonFlow::validate() const
    {
        validateGroupHint();
    }

    //
    // Validates that the group hint tag is present and valid
    // See https://specs.amwa.tv/nmos-parameter-registers/branches/main/tags/grouphint.html
    //
    void NMOSCommonFlow::validateGroupHint() const
    {
        auto groupHints = tags.get().groupHints.get();

        // we need at least a group hint
        if (groupHints.empty())
        {
            std::invalid_argument("Group hint tag found but empty.");
        }
        else
        {
            // iterate over the array and confirm that all values are strings and follow the
            // expected format.
            // "<group-name>:<role-in-group>[:<group-scope>]" where group-scope if present, is either device or node
            for (auto const& hint : groupHints)
            {
                // Get the array item
                // Split the string into parts separated by ':'
                auto parts = hint | std::ranges::views::split(':') |
                             std::views::transform([&](auto&& rng) { return std::string_view(&*rng.begin(), std::ranges::distance(rng)); });

                auto vec = std::vector<std::string_view>{parts.begin(), parts.end()};
                if ((vec.size() < 2) || (vec.size() > 3))
                {
                    throw std::invalid_argument(
                        fmt::format("Invalid group hint value '{}'. Expected format '<group-name>:<role-in-group>[:<group-scope>]'", hint));
                }

                // Validate the group name and role
                auto const& groupName = vec[0];
                auto const& role = vec[1];
                if (groupName.empty() || role.empty())
                {
                    throw std::invalid_argument(fmt::format("Invalid group hint value '{}'. Group name and role must not be empty.", hint));
                }

                // Validate the group scope if present
                if (vec.size() == 3)
                {
                    auto const& groupScope = vec[2];
                    if (groupScope != "device" && groupScope != "node")
                    {
                        throw std::invalid_argument(
                            fmt::format("Invalid group hint value '{}'. Group scope must be either 'device' or 'node'.", hint));
                    }
                }
            }
            // all the tags passed validation
        }
    }

    std::string_view NMOSVideoFlow::getDescription() const noexcept
    {
        return common.get().getDescription();
    }

    uuids::uuid NMOSVideoFlow::getId() const
    {
        return common.get().getId();
    }

    std::string_view NMOSVideoFlow::getLabel() const noexcept
    {
        return common.get().getLabel();
    }

    std::string_view NMOSVideoFlow::getMediaType() const noexcept
    {
        return common.get().getMediaType();
    }

    std::vector<std::string> const& NMOSVideoFlow::getGroupHints() const noexcept
    {
        return common.get().getGroupHints();
    }

    mxlRational NMOSVideoFlow::getGrainRate() const noexcept
    {
        if (isInterlaced())
        {
            auto grainRateTmp = grainRate.get();
            grainRateTmp.numerator *= 2; // In interlace, the grainRate is actually the field rate

            return grainRateTmp.toMxl();
        }

        return grainRate.get().toMxl();
    }

    std::uint32_t NMOSVideoFlow::getFrameWidth() const noexcept
    {
        return frameWidth.get().value();
    }

    std::uint32_t NMOSVideoFlow::getFrameHeight() const noexcept
    {
        return frameWidth.get().value();
    }

    std::string_view NMOSVideoFlow::getColorspace() const noexcept
    {
        return colorspace.get();
    }

    bool NMOSVideoFlow::isInterlaced() const noexcept
    {
        return interlaceMode.get() != "progressive";
    }

    std::size_t NMOSVideoFlow::getPayloadSize() const
    {
        if (getMediaType() == "video/v210")
        {
            auto frameH = getFrameHeight();
            auto frameW = getFrameWidth();

            if (!isInterlaced() || ((frameH % 2) == 0))
            {
                // Interlaced media is handled as separate fields.
                auto const h = isInterlaced() ? frameH / 2 : frameH;
                return static_cast<std::size_t>((frameW + 47) / 48 * 128) * h;
            }
            else
            {
                throw std::invalid_argument{"Invalid video height for interlaced v210. Must be even."};
            }
        }
        else
        {
            throw std::invalid_argument{fmt::format("Unsupported video media_type: {}", getMediaType())};
        }
    }

    std::size_t NMOSVideoFlow::getPayloadSliceLength() const
    {
        // For video flows the slice length is the byte-length of a single
        // line of v210 video.
        if (getMediaType() != "video/v210")
        {
            throw std::invalid_argument{fmt::format("Unsupported video media_type: {}", getMediaType())};
        }

        return static_cast<std::size_t>((getFrameWidth() + 47) / 48 * 128);
    }

    std::size_t NMOSVideoFlow::getTotalPayloadSlices() const
    {
        if (getMediaType() != "video/v210")
        {
            throw std::invalid_argument{fmt::format("Unsupported video media_type: {}", getMediaType())};
        }

        // For v210, the number of slices is always the number of video lines
        if (isInterlaced())
        {
            return getFrameHeight() / 2;
        }

        return getFrameHeight();
    }

    void NMOSVideoFlow::validate() const
    {
        common.get().validate();
        validateGrainRate();
    }

    void NMOSVideoFlow::validateGrainRate() const
    {
        if ((interlaceMode.get() == "interlaced_tff") || (interlaceMode.get() == "interlaced_bff"))
        {
            // This is an interlaced video flow.  confirm that the grain rate is defined to 30000/1001 or 25/1
            if ((grainRate.get() != Rational{.numerator = 30000, .denominator = 1001}) &&
                (grainRate.get() != Rational{.numerator = 25, .denominator = 1}))
            {
                throw std::invalid_argument{"Invalid grain_rate for interlaced video. Expected 30000/1001 or 25/1."};
            }
        }
    }

    std::string_view NMOSAudioFlow::getDescription() const noexcept
    {
        return common.get().getDescription();
    }

    uuids::uuid NMOSAudioFlow::getId() const
    {
        return common.get().getId();
    }

    std::string_view NMOSAudioFlow::getLabel() const noexcept
    {
        return common.get().getLabel();
    }

    std::string_view NMOSAudioFlow::getMediaType() const noexcept
    {
        return common.get().getMediaType();
    }

    std::vector<std::string> const& NMOSAudioFlow::getGroupHints() const noexcept
    {
        return common.get().getGroupHints();
    }

    mxlRational NMOSAudioFlow::getSampleRate() const noexcept
    {
        return sampleRate.get().toMxl();
    }

    std::uint32_t NMOSAudioFlow::getChannelCount() const noexcept
    {
        return channelCount.get();
    }

    std::uint32_t NMOSAudioFlow::getBitDepth() const noexcept
    {
        return bitDepth.get();
    }

    uuids::uuid NMOSAudioFlow::getSourceId() const
    {
        return *uuids::uuid::from_string(sourceId.get().value());
    }

    uuids::uuid NMOSAudioFlow::getDeviceId() const
    {
        return *uuids::uuid::from_string(deviceId.get().value());
    }

    std::size_t NMOSAudioFlow::getPayloadSize() const
    {
        // TODO: Also check the media type once we agreed on how to encode
        //      single precision IEEE floats.
        if ((bitDepth.get() != 32.0) && (bitDepth.get() != 64.0))
        {
            auto msg = fmt::format("Unsupported bit depth: {}", bitDepth.get());
            throw std::invalid_argument{std::move(msg)};
        }

        return static_cast<std::size_t>(bitDepth.get()) / 8U;
    }

    void NMOSAudioFlow::validate() const
    {
        common.get().validate();
    }

    std::string_view NMOSDataFlow::getDescription() const noexcept
    {
        return common.get().getDescription();
    }

    uuids::uuid NMOSDataFlow::getId() const
    {
        return common.get().getId();
    }

    std::string_view NMOSDataFlow::getLabel() const noexcept
    {
        return common.get().getLabel();
    }

    std::string_view NMOSDataFlow::getMediaType() const noexcept
    {
        return common.get().getMediaType();
    }

    std::vector<std::string> const& NMOSDataFlow::getGroupHints() const noexcept
    {
        return common.get().getGroupHints();
    }

    mxlRational NMOSDataFlow::getGrainRate() const noexcept
    {
        return grainRate.get().toMxl();
    }

    std::size_t NMOSDataFlow::getPayloadSize() const
    {
        if (common.get().mediaType.get() == "video/smpte291")
        {
            // This is large enough to hold all the ANC data in a single grain.
            // This size is an usual VFS page. no point at going smaller.
            return DATA_FORMAT_GRAIN_SIZE;
        }
        else
        {
            throw std::invalid_argument{fmt::format("Unsupported  media_type: {}", common.get().mediaType.get())};
        }
    }

    std::size_t NMOSDataFlow::getPayloadSliceLength() const
    {
        return 1;
    }

    std::size_t NMOSDataFlow::getTotalPayloadSlices() const
    {
        return DATA_FORMAT_GRAIN_SIZE;
    }

    void NMOSDataFlow::validate() const
    {
        common.get().validate();
    }

    NMOSFlow NMOSFlow::fromStr(std::string_view s)
    {
        auto inner = rfl::json::read<NMOSFlow::Inner>(s);
        if (inner)
        {
            auto nmosFlow = NMOSFlow(*inner);
            nmosFlow.validate();
            return nmosFlow;
        }
        else
        {
            auto err = inner.error();
            throw std::invalid_argument(fmt::format("failed to parse NMOS json file. {}", err.what()));
        }
    }

    NMOSFlow NMOSFlow::fromVideo(NMOSVideoFlow flow)
    {
        return {flow};
    }

    NMOSFlow NMOSFlow::fromAudio(NMOSAudioFlow flow)
    {
        return {flow};
    }

    NMOSFlow NMOSFlow::fromData(NMOSDataFlow flow)
    {
        return {flow};
    }

    std::string NMOSFlow::toJson() const
    {
        return rfl::json::write(_inner);
    }

    std::string_view NMOSFlow::getDescription() const noexcept
    {
        return rfl::visit(
            overloaded{
                [](NMOSVideoFlow const& flow) { return flow.getDescription(); },
                [](NMOSAudioFlow const& flow) { return flow.getDescription(); },
                [](NMOSDataFlow const& flow) { return flow.getDescription(); },

            },
            _inner.variant());
    }

    uuids::uuid NMOSFlow::getId() const
    {
        return rfl::visit(
            overloaded{
                [](NMOSVideoFlow const& flow) { return flow.getId(); },
                [](NMOSAudioFlow const& flow) { return flow.getId(); },
                [](NMOSDataFlow const& flow) { return flow.getId(); },

            },
            _inner.variant());
    }

    std::string_view NMOSFlow::getLabel() const noexcept
    {
        return rfl::visit(
            overloaded{
                [](NMOSVideoFlow const& flow) { return flow.getDescription(); },
                [](NMOSAudioFlow const& flow) { return flow.getDescription(); },
                [](NMOSDataFlow const& flow) { return flow.getDescription(); },

            },
            _inner.variant());
    }

    std::string_view NMOSFlow::getMediaType() const noexcept
    {
        return rfl::visit(
            overloaded{
                [](NMOSVideoFlow const& flow) { return flow.getDescription(); },
                [](NMOSAudioFlow const& flow) { return flow.getDescription(); },
                [](NMOSDataFlow const& flow) { return flow.getDescription(); },

            },
            _inner.variant());
    }

    std::vector<std::string> NMOSFlow::getGroupHints() const noexcept
    {
        return rfl::visit(
            overloaded{
                [](NMOSVideoFlow const& flow) { return flow.getGroupHints(); },
                [](NMOSAudioFlow const& flow) { return flow.getGroupHints(); },
                [](NMOSDataFlow const& flow) { return flow.getGroupHints(); },

            },
            _inner.variant());
    }

    bool NMOSFlow::isVideo() const noexcept
    {
        return rfl::holds_alternative<NMOSVideoFlow>(_inner.variant());
    }

    NMOSVideoFlow const& NMOSFlow::asVideo() const
    {
        return rfl::get<NMOSVideoFlow>(_inner.variant());
    }

    bool NMOSFlow::isAudio() const noexcept
    {
        return rfl::holds_alternative<NMOSAudioFlow>(_inner.variant());
    }

    NMOSAudioFlow const& NMOSFlow::asAudio() const
    {
        return rfl::get<NMOSAudioFlow>(_inner.variant());
    }

    bool NMOSFlow::isData() const noexcept
    {
        return rfl::holds_alternative<NMOSDataFlow>(_inner.variant());
    }

    NMOSDataFlow const& NMOSFlow::asData() const
    {
        return rfl::get<NMOSDataFlow>(_inner.variant());
    }

    mxlDataFormat NMOSFlow::getFormat() const
    {
        return rfl::visit(
            overloaded{
                [](NMOSVideoFlow const&) { return mxlDataFormat::MXL_DATA_FORMAT_VIDEO; },
                [](NMOSAudioFlow const&) { return mxlDataFormat::MXL_DATA_FORMAT_AUDIO; },
                [](NMOSDataFlow const&) { return mxlDataFormat::MXL_DATA_FORMAT_DATA; },

            },
            _inner.variant());
    }

    NMOSFlow::NMOSFlow(Inner inner)
        : _inner(std::move(inner))
    {}

    void NMOSFlow::validate()
    {
        rfl::visit(overloaded{[](NMOSVideoFlow const& flow) { flow.validate(); },
                       [](NMOSAudioFlow const& flow) { flow.validate(); },
                       [](NMOSDataFlow const& flow) { flow.validate(); }},
            _inner.variant());
    }

}
