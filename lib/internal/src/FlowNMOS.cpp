#include <mxl-internal/FlowNMOS.hpp>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <uuid.h>
#include <fmt/color.h>
#include <rfl/json/read.hpp>
#include <rfl/TaggedUnion.hpp>
#include <rfl/Variant.hpp>
#include <rfl/visit.hpp>
#include "mxl/dataformat.h"
#include "mxl/rational.h"

namespace mxl::lib
{

    mxlRational Rational::toMxlApi() const
    {
        return {.numerator = numerator, .denominator = denominator};
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

    uuids::uuid NMOSCommonFlow::getId() const
    {
        return *uuids::uuid::from_string(id.get().value());
    }

    mxlRational NMOSVideoFlow::getGrainRate() const
    {
        return grainRate.get().toMxlApi();
    }

    std::size_t NMOSVideoFlow::getPayloadSize() const
    {
        auto payloadSize = std::size_t{0};

        if (common.get().mediaType.get() == "video/v210")
        {
            auto frameH = frameHeight.get().value();
            auto frameW = frameWidth.get().value();

            if (!isInterlaced() || ((frameH % 2) == 0))
            {
                // Interlaced media is handled as separate fields.
                auto const h = isInterlaced() ? frameH / 2 : frameH;
                payloadSize = static_cast<std::size_t>((frameW + 47) / 48 * 128) * h;
            }
            else
            {
                auto msg = std::string{"Invalid video height for interlaced v210. Must be even."};
                throw std::invalid_argument{std::move(msg)};
            }
        }
        else
        {
            auto msg = std::string{"Unsupported video media_type: "} + std::string(common.get().mediaType.get());
            throw std::invalid_argument{std::move(msg)};
        }

        return payloadSize;
    }

    std::size_t NMOSVideoFlow::getPayloadSliceLength() const
    {
        // For video flows the slice length is the byte-length of a single
        // line of v210 video.
        if (common.get().mediaType.get() != "video/v210")
        {
            auto msg = std::string{"Unsupported video media_type: "} + std::string(common.get().mediaType.get());
            throw std::invalid_argument{std::move(msg)};
        }

        return static_cast<std::size_t>((frameWidth.get().value() + 47) / 48 * 128);
    }

    std::size_t NMOSVideoFlow::getTotalPayloadSlices() const
    {
        if (common.get().mediaType.get() != "video/v210")
        {
            auto msg = std::string{"Unsupported video media_type: "} + std::string(common.get().mediaType.get());
            throw std::invalid_argument{std::move(msg)};
        }

        // For v210, the number of slices is always the number of video lines
        if (isInterlaced())
        {
            return frameHeight.get().value() / 2;
        }

        return frameHeight.get().value();
    }

    mxlRational NMOSAudioFlow::getSampleRate() const
    {
        return sampleRate.get().toMxlApi();
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

    bool NMOSVideoFlow::isInterlaced() const
    {
        return interlaceMode.get() != "progressive";
    }

    NMOSFlow NMOSFlow::fromStr(std::string_view s)
    {
        auto inner = rfl::json::read<NMOSFlow::Inner>(s);
        if (inner)
        {
            return NMOSFlow{*inner};
        }
        else
        {
            auto err = inner.error();
            throw std::invalid_argument(fmt::format("failed to parse NMOS json file. {}", err.what()));
        }
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

    mxlDataFormat NMOSFlow::getFormat() const
    {
        if (rfl::holds_alternative<NMOSVideoFlow>(_inner.variant()))
        {
            return mxlDataFormat::MXL_DATA_FORMAT_VIDEO;
        }
        else if (rfl::holds_alternative<NMOSAudioFlow>(_inner.variant()))
        {
            return mxlDataFormat::MXL_DATA_FORMAT_AUDIO;
        }
        else
        {
            // Should not be possible to end-up here
            throw std::runtime_error("unsupported data format");
        }
    }

    NMOSFlow::NMOSFlow(Inner inner)
        : _inner(std::move(inner))
    {}

}
