#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <rfl.hpp>
#include <uuid.h>
#include <rfl/json.hpp>
#include <rfl/Rename.hpp>
#include "mxl/dataformat.h"
#include "mxl/rational.h"

namespace mxl::lib
{
    // this are arbitrary limits, but we need to put a cap somewhere to prevent a bad json document
    // from allocating all the RAM on the system.
    constexpr auto MAX_VIDEO_FRAME_WIDTH = 7680u;  // 8K UHD
    constexpr auto MAX_VIDEO_FRAME_HEIGHT = 4320u; // 8K UHD

    // Grain size when the grain data format is "data"
    constexpr auto DATA_FORMAT_GRAIN_SIZE = 4096;

    struct Rational
    {
    public:
        [[nodiscard]]
        mxlRational toMxlApi() const;

    public:
        std::int64_t numerator;
        std::int64_t denominator;

        struct Rfl
        {
            static Rfl from_class(Rational const& p) noexcept;

            [[nodiscard]]
            Rational to_class() const;

            rfl::Rename<"numerator", std::int64_t> numerator;
            rfl::Rename<"denominator", std::optional<std::int64_t>> denominator;
        };
    };

    struct NMOSCommonFlow
    {
        rfl::Rename<"description", std::string> description;
        rfl::Rename<"id", rfl::UUIDv4> id;
        // TODO: tags
        rfl::Rename<"label", std::string> label;
        rfl::Rename<"media_type", std::string> mediaType;

        [[nodiscard]]
        uuids::uuid getId() const;
    };

    struct NMOSVideoFlow
    {
    public:
        using Tag = rfl::Literal<"urn:x-nmos:format:video">;
        using MaxFrameWidth = rfl::Validator<std::uint32_t, rfl::Maximum<MAX_VIDEO_FRAME_WIDTH>>;
        using MaxFrameHeight = rfl::Validator<std::uint32_t, rfl::Maximum<MAX_VIDEO_FRAME_HEIGHT>>;

    public:
        /**
         * Accessor for the 'grain_rate' field
         * \return The grain rate if found and valid.
         */
        [[nodiscard]]
        mxlRational getGrainRate() const;

        /**
         * Computes the grain payload size
         * \return The payload size
         */
        [[nodiscard]]
        std::size_t getPayloadSize() const;

        /**
         *  Computes the length of a slice of the payload.
         *  \return The length of a slice
         */
        [[nodiscard]]
        std::size_t getPayloadSliceLength() const;

        /**
         * Computes the number of slices that make up a full grain.
         * \return The number of slices
         */
        [[nodiscard]]
        std::size_t getTotalPayloadSlices() const;

    public:
        rfl::Flatten<NMOSCommonFlow> common;
        rfl::Rename<"grain_rate", Rational> grainRate;
        rfl::Rename<"frame_width", MaxFrameWidth> frameWidth;
        rfl::Rename<"frame_height", MaxFrameHeight> frameHeight;
        rfl::Rename<"interlace_mode", rfl::Literal<"interlaced_tff", "interlaced_bff", "progressive">> interlaceMode;
        rfl::Rename<"colorspace", std::string> colorspace;

    private:
        [[nodiscard]]
        bool isInterlaced() const;
    };

    struct NMOSAudioFlow
    {
    public:
        using Tag = rfl::Literal<"urn:x-nmos:format:audio">;

    public:
        /**
         * Accessor for the 'sample_rate' field
         * \return The sample rate if found and valid.
         */
        [[nodiscard]]
        mxlRational getSampleRate() const;
        /**
         *
         * Computes the payload size
         * \return The payload size
         */
        [[nodiscard]]
        std::size_t getPayloadSize() const;

    public:
        rfl::Flatten<NMOSCommonFlow> common;
        rfl::Rename<"sample_rate", Rational> sampleRate;
        rfl::Rename<"channel_count", std::uint32_t> channelCount;
        rfl::Rename<"bit_depth", std::uint32_t> bitDepth;
        rfl::Rename<"source_id", rfl::UUIDv4> sourceId;
        rfl::Rename<"device_id", rfl::UUIDv4> deviceId;
    };

    class NMOSFlow
    {
    public:
        static NMOSFlow fromStr(std::string_view s);

        [[nodiscard]]
        bool isVideo() const noexcept;

        [[nodiscard]]
        NMOSVideoFlow const& asVideo() const;

        [[nodiscard]]
        bool isAudio() const noexcept;

        [[nodiscard]]
        NMOSAudioFlow const& asAudio() const;

        [[nodiscard]]
        mxlDataFormat getFormat() const;

        using Inner = rfl::TaggedUnion<"format", NMOSVideoFlow, NMOSAudioFlow>;

    private:
        NMOSFlow(Inner);

    private:
        Inner _inner;
    };

}

namespace rfl::parsing
{
    template<class ReaderType, class WriterType, class ProcessorsType>
    struct Parser<ReaderType, WriterType, mxl::lib::Rational, ProcessorsType>
        : public CustomParser<ReaderType, WriterType, ProcessorsType, mxl::lib::Rational, mxl::lib::Rational::Rfl>
    {};
}
