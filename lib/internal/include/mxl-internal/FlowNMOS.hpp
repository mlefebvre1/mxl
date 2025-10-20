#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <rfl.hpp>
#include <uuid.h>
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
        mxlRational toMxl() const noexcept;
        static Rational fromMxl(mxlRational mxl) noexcept;

        bool operator==(Rational const& rhs) const noexcept;

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
    public:
        struct NMOSTags
        {
            rfl::Rename<"urn:x-nmos:tag:grouphint/v1.0", std::vector<std::string>> groupHints;
        };

    public:
        [[nodiscard]]
        std::string_view getDescription() const noexcept;

        [[nodiscard]]
        uuids::uuid getId() const;

        [[nodiscard]]
        std::string_view getLabel() const noexcept;

        [[nodiscard]]
        std::string_view getMediaType() const noexcept;

        [[nodiscard]]
        std::vector<std::string> const& getGroupHints() const noexcept;

    public:
        rfl::Rename<"description", std::string> description;
        rfl::Rename<"id", rfl::UUIDv4> id;
        rfl::Rename<"tags", NMOSTags> tags;
        rfl::Rename<"label", std::string> label;
        rfl::Rename<"media_type", std::string> mediaType;

    private:
        friend struct NMOSVideoFlow;
        friend struct NMOSAudioFlow;
        friend struct NMOSDataFlow;

    private:
        void validate() const;
        void validateGroupHint() const;
    };

    struct NMOSVideoFlow
    {
    public:
        using MaxFrameWidth = rfl::Validator<std::uint32_t, rfl::Maximum<MAX_VIDEO_FRAME_WIDTH>>;
        using MaxFrameHeight = rfl::Validator<std::uint32_t, rfl::Maximum<MAX_VIDEO_FRAME_HEIGHT>>;

    public:
        struct Component
        {
            rfl::Rename<"name", std::string> name;
            rfl::Rename<"width", MaxFrameWidth> width;
            rfl::Rename<"height", MaxFrameHeight> height;
            rfl::Rename<"bit_depth", std::uint32_t> bit_depth;
        };

    public:
        using Tag = rfl::Literal<"urn:x-nmos:format:video">;

    public:
        [[nodiscard]]
        std::string_view getDescription() const noexcept;

        [[nodiscard]]
        uuids::uuid getId() const;

        [[nodiscard]]
        std::string_view getLabel() const noexcept;

        [[nodiscard]]
        std::string_view getMediaType() const noexcept;

        [[nodiscard]]
        std::vector<std::string> const& getGroupHints() const noexcept;
        /**
         * Accessor for the 'grain_rate' field
         * \return The grain rate if found and valid.
         */
        [[nodiscard]]
        mxlRational getGrainRate() const noexcept;

        [[nodiscard]]
        std::uint32_t getFrameWidth() const noexcept;

        [[nodiscard]]
        std::uint32_t getFrameHeight() const noexcept;

        [[nodiscard]]
        std::string_view getColorspace() const noexcept;

        [[nodiscard]]
        bool isInterlaced() const noexcept;
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

        void validate() const;

        rfl::Flatten<NMOSCommonFlow> common;
        rfl::Rename<"grain_rate", Rational> grainRate;
        rfl::Rename<"frame_width", MaxFrameWidth> frameWidth;
        rfl::Rename<"frame_height", MaxFrameHeight> frameHeight;
        rfl::Rename<"interlace_mode", rfl::Literal<"interlaced_tff", "interlaced_bff", "progressive">> interlaceMode;
        rfl::Rename<"colorspace", std::string> colorspace;
        rfl::Rename<"components", std::vector<Component>> components;

    private:
        void validateGrainRate() const;
    };

    struct NMOSAudioFlow
    {
    public:
        using Tag = rfl::Literal<"urn:x-nmos:format:audio">;

    public:
        [[nodiscard]]
        std::string_view getDescription() const noexcept;

        [[nodiscard]]
        uuids::uuid getId() const;

        [[nodiscard]]
        std::string_view getLabel() const noexcept;

        [[nodiscard]]
        std::string_view getMediaType() const noexcept;

        [[nodiscard]]
        std::vector<std::string> const& getGroupHints() const noexcept;
        /**
         * Accessor for the 'sample_rate' field
         * \return The sample rate if found and valid.
         */
        [[nodiscard]]
        mxlRational getSampleRate() const noexcept;

        [[nodiscard]]
        std::uint32_t getChannelCount() const noexcept;

        [[nodiscard]]
        std::uint32_t getBitDepth() const noexcept;

        [[nodiscard]]
        uuids::uuid getSourceId() const;

        [[nodiscard]]
        uuids::uuid getDeviceId() const;

        /**
         *
         * Computes the payload size
         * \return The payload size
         */
        [[nodiscard]]
        std::size_t getPayloadSize() const;

        void validate() const;

    public:
        rfl::Flatten<NMOSCommonFlow> common;
        rfl::Rename<"sample_rate", Rational> sampleRate;
        rfl::Rename<"channel_count", std::uint32_t> channelCount;
        rfl::Rename<"bit_depth", std::uint32_t> bitDepth;
        rfl::Rename<"source_id", rfl::UUIDv4> sourceId;
        rfl::Rename<"device_id", rfl::UUIDv4> deviceId;
    };

    struct NMOSDataFlow
    {
    public:
        using Tag = rfl::Literal<"urn:x-nmos:format:data">;

    public:
        [[nodiscard]]
        std::string_view getDescription() const noexcept;

        [[nodiscard]]
        uuids::uuid getId() const;

        [[nodiscard]]
        std::string_view getLabel() const noexcept;

        [[nodiscard]]
        std::string_view getMediaType() const noexcept;

        [[nodiscard]]
        std::vector<std::string> const& getGroupHints() const noexcept;

        /**
         * Accessor for the 'grain_rate' field
         * \return The grain rate if found and valid.
         */
        [[nodiscard]]
        mxlRational getGrainRate() const noexcept;

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
        void validate() const;

    public:
        rfl::Flatten<NMOSCommonFlow> common;
        rfl::Rename<"grain_rate", Rational> grainRate;
    };

    class NMOSFlow
    {
    public:
        static NMOSFlow fromStr(std::string_view s);
        static NMOSFlow fromVideo(NMOSVideoFlow flow);
        static NMOSFlow fromAudio(NMOSAudioFlow flow);
        static NMOSFlow fromData(NMOSDataFlow flow);

        [[nodiscard]]
        std::string toJson() const;

        [[nodiscard]]
        std::string_view getDescription() const noexcept;

        [[nodiscard]]
        uuids::uuid getId() const;

        [[nodiscard]]
        std::string_view getLabel() const noexcept;

        [[nodiscard]]
        std::string_view getMediaType() const noexcept;

        [[nodiscard]]
        std::vector<std::string> getGroupHints() const noexcept;

        [[nodiscard]]
        bool isVideo() const noexcept;

        [[nodiscard]]
        NMOSVideoFlow const& asVideo() const;

        [[nodiscard]]
        bool isAudio() const noexcept;

        [[nodiscard]]
        NMOSAudioFlow const& asAudio() const;

        [[nodiscard]]
        bool isData() const noexcept;

        [[nodiscard]]
        NMOSDataFlow const& asData() const;

        [[nodiscard]]
        mxlDataFormat getFormat() const;

    private:
        using Inner = rfl::TaggedUnion<"format", NMOSVideoFlow, NMOSAudioFlow, NMOSDataFlow>;

        NMOSFlow(Inner);

        void validate();

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
