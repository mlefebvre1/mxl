// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>
#include <glib.h>
#include <CLI/CLI.hpp>
#include <glib-object.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/gstbin.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstelement.h>
#include <gst/gstelementfactory.h>
#include <gst/gstformat.h>
#include <gst/gstmemory.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstpipeline.h>
#include <gst/gstutils.h>
#include <gst/gstvalue.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/FlowParser.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

#ifdef __APPLE__
#   include <TargetConditionals.h>
#endif

std::sig_atomic_t volatile g_exit_requested = 0;

void signal_handler(int)
{
    g_exit_requested = 1;
}

struct GstreamerAudioPipelineConfig
{
    mxlRational rate;
    std::size_t channelCount;
    std::vector<size_t> spkrEnabled;
};

struct GstreamerPipelineConfig
{
    std::optional<GstreamerAudioPipelineConfig> audio_config;
};

/// The returned caps have to be freed with gst_caps_unref().
GstCaps* gstCapsFromAudioConfig(GstreamerAudioPipelineConfig const& config)
{
    auto* audioInfo = gst_audio_info_new();

    std::vector<GstAudioChannelPosition> chanPositions;
    for (size_t i = 0; i < config.channelCount; i++)
    {
        chanPositions.emplace_back(static_cast<GstAudioChannelPosition>(i));
    }

    gst_audio_info_set_format(audioInfo, GST_AUDIO_FORMAT_F32LE, config.rate.numerator, config.channelCount, chanPositions.data());
    audioInfo->layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;

    auto* caps = gst_audio_info_to_caps(audioInfo);
    gst_audio_info_free(audioInfo);
    return caps;
}

/// Encapsulation of the Gstreamer pipeline used to play data received from MXL.
///
/// The current implementation assumes that the video is always present and that the audio is optional.
class GstreamerPipeline final
{
public:
    GstreamerPipeline(GstreamerPipelineConfig const& config)
    {
        gst_init(nullptr, nullptr);

        _pipeline = gst_pipeline_new("test-pipeline");
        if (!_pipeline)
        {
            throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
        }

        if (config.audio_config)
        {
            std::string pipelineDesc = fmt::format(
                "appsrc name=appsrc ! "
                "audio/x-raw,format=F32LE,layout=non-interleaved,channels={},rate=48000 ! "
                "audioconvert mix-matrix=\"< ",
                config.audio_config->channelCount);

            for (size_t spkrId = 0; spkrId < config.audio_config->spkrEnabled.size(); spkrId++)
            {
                auto spkrEn = config.audio_config->spkrEnabled[spkrId];

                pipelineDesc += "< ";
                for (size_t chan = 0; chan < config.audio_config->channelCount; chan++)
                {
                    if (chan == spkrEn)
                    {
                        pipelineDesc += "(float)1";
                    }
                    else
                    {
                        pipelineDesc += "(float)0";
                    }

                    if (chan != config.audio_config->channelCount - 1)
                    {
                        pipelineDesc += ", ";
                    }
                }
                pipelineDesc += " >";

                if (spkrId != config.audio_config->spkrEnabled.size() - 1)
                {
                    pipelineDesc += ", ";
                }
            }
            pipelineDesc += ">\" ! "
                            "autoaudiosink";

            MXL_INFO("Generating gsteamer pipeline -> {}", pipelineDesc);

            GError* error = nullptr;
            _pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
            if (!_pipeline || error)
            {
                MXL_ERROR("Failed to create pipeline: {}", error->message);
                g_error_free(error);
                throw std::runtime_error("Gstreamer: 'pipeline' could not be created.");
            }

            _audioAppsrc = gst_bin_get_by_name(GST_BIN(_pipeline), "appsrc");
            if (_audioAppsrc == nullptr)
            {
                throw std::runtime_error("Gstreamer: 'appsink' could not be found in the pipeline.");
            }

            _audioCaps = gstCapsFromAudioConfig(*config.audio_config);
            g_object_set(G_OBJECT(_audioAppsrc), "caps", _audioCaps, "format", GST_FORMAT_TIME, nullptr);
        }

        _audioConfig = config.audio_config;
    }

    ~GstreamerPipeline()
    {
        if (_audioCaps)
        {
            gst_caps_unref(_audioCaps);
        }
        if (_pipeline)
        {
            gst_element_set_state(_pipeline, GST_STATE_NULL);
            gst_object_unref(_pipeline);
        }
        // The pipeline owns all the elements, we do not need to free them individually.

        gst_deinit();
    }

    void start()
    {
        // Start playing
        gst_element_set_state(_pipeline, GST_STATE_PLAYING);
    }

    void pushAudioSamples(mxlWrappedMultiBufferSlice const& payload)
    {
        auto const oneChannelBufferSize{payload.base.fragments[0].size + payload.base.fragments[1].size};
        gsize payloadLen{oneChannelBufferSize * payload.count};
        GstBuffer* gstBuffer{gst_buffer_new_allocate(nullptr, payloadLen, nullptr)};

        // This is as inefficient as you get it, but hey, first iteration...
        GstAudioInfo audioInfo;
        if (!gst_audio_info_from_caps(&audioInfo, _audioCaps))
        {
            MXL_ERROR("Error while creating audio info from caps.");
            gst_buffer_unref(gstBuffer);
            return;
        };
        auto const numOfSamples{oneChannelBufferSize / sizeof(float)};
        GstAudioMeta* audioMeta{gst_buffer_add_audio_meta(gstBuffer, &audioInfo, numOfSamples, nullptr)};
        if (!audioMeta)
        {
            MXL_ERROR("Error while adding meta to audio buffer.");
            gst_buffer_unref(gstBuffer);
            return;
        }
        GstAudioBuffer audioBuffer;
        if (!gst_audio_buffer_map(&audioBuffer, &audioInfo, gstBuffer, GST_MAP_WRITE))
        {
            MXL_ERROR("Error while mapping audio buffer.");
            gst_buffer_unref(gstBuffer);
            return;
        }

        for (std::size_t channel = 0; channel < payload.count; ++channel)
        {
            auto currentWritePtr{static_cast<std::byte*>(audioBuffer.planes[channel])};
            auto const readPtr0{static_cast<std::byte const*>(payload.base.fragments[0].pointer) + channel * payload.stride};
            auto const readSize0{payload.base.fragments[0].size};
            ::memcpy(currentWritePtr, readPtr0, readSize0);
            currentWritePtr += readSize0;

            auto const readPtr1{static_cast<std::byte const*>(payload.base.fragments[1].pointer) + channel * payload.stride};
            auto const readSize1{payload.base.fragments[1].size};
            ::memcpy(currentWritePtr, readPtr1, readSize1);
        }

        gst_audio_buffer_unmap(&audioBuffer);
        MXL_DEBUG("Pushing {} audio samples with PTS: {} ns", oneChannelBufferSize / 4, GST_BUFFER_PTS(gstBuffer));

        int ret;
        g_signal_emit_by_name(_audioAppsrc, "push-buffer", gstBuffer, &ret);
        if (ret != GST_FLOW_OK)
        {
            MXL_ERROR("Error pushing buffer to video appsrc");
        }

        gst_buffer_unref(gstBuffer);
    }

private:
    GstElement* _audioAppsrc{nullptr};
    GstElement* _pipeline{nullptr};

    /// The following information is needed when passing audio samples in non-interleaved formate.
    std::optional<GstreamerAudioPipelineConfig> _audioConfig{std::nullopt};
    /// We use the caps to initialize the audio metadata when pushing audio samples. There are probably more efficient ways to do this, but this
    /// should be good enough for the first iteration.
    GstCaps* _audioCaps{nullptr};
};

std::string read_flow_descriptor(std::string const& domain, std::string const& flowID)
{
    auto const descriptor_path = mxl::lib::makeFlowDescriptorFilePath(domain, flowID);
    if (!std::filesystem::exists(descriptor_path))
    {
        throw std::runtime_error{fmt::format("Flow descriptor file '{}' does not exist.", descriptor_path.string())};
    }

    std::ifstream descriptor_reader(descriptor_path);
    return std::string{(std::istreambuf_iterator<char>(descriptor_reader)), std::istreambuf_iterator<char>()};
}

GstreamerPipelineConfig prepare_gstreamer_config(std::string const& domain, std::optional<std::string> const& audioFlowID,
    std::vector<size_t> const& listenChannels)
{
    std::optional<GstreamerAudioPipelineConfig> audio_config;
    if (audioFlowID)
    {
        std::string audio_flow_descriptor{read_flow_descriptor(domain, *audioFlowID)};
        mxl::lib::FlowParser audio_descriptor_parser{audio_flow_descriptor};
        audio_config = GstreamerAudioPipelineConfig{
            .rate = audio_descriptor_parser.getGrainRate(),
            .channelCount = audio_descriptor_parser.getChannelCount(),
            .spkrEnabled = listenChannels,
        };
    }
    else
    {
        audio_config = std::nullopt;
    }

    return GstreamerPipelineConfig{.audio_config = audio_config};
}

int real_main(int argc, char** argv, void*)
{
    std::signal(SIGINT, &signal_handler);
    std::signal(SIGTERM, &signal_handler);

    CLI::App app("mxl-gst-audiosink");

    std::optional<std::string> audioFlowID;
    auto audioFlowIDOpt = app.add_option("-a, --audio-flow-id", audioFlowID, "The audio flow ID");
    audioFlowIDOpt->required(false);

    std::vector<std::size_t> listenChannels;
    auto listenChanOpt = app.add_option("-l, --listen-channels", listenChannels, "Audio channels to listen");
    listenChanOpt->default_val(std::vector<std::size_t>{0, 1});

    std::string domain;
    auto domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->required(true);
    domainOpt->check(CLI::ExistingDirectory);

    uint64_t samplesPerBatch;
    auto samplesPerBatchOpt = app.add_option("-s, --samples-per-batch", samplesPerBatch, "Number of audio samples per batch");
    samplesPerBatchOpt->default_val(1024);

    int64_t sampleOffset;
    auto sampleOffsetOpt = app.add_option(
        "-o, --sample-offset", sampleOffset, "Audio offset in samples. Positive value means you are adding a delay");
    sampleOffsetOpt->default_val(0);

    CLI11_PARSE(app, argc, argv);

    GstreamerPipelineConfig gst_config{prepare_gstreamer_config(domain, audioFlowID, listenChannels)};
    GstreamerPipeline gst_pipeline(gst_config);
    uint64_t headIndex;

    int exit_status = EXIT_SUCCESS;
    mxlStatus ret;

    mxlFlowReader audioReader{nullptr};
    auto instance = mxlCreateInstance(domain.c_str(), "");
    if (instance == nullptr)
    {
        MXL_ERROR("Failed to create MXL instance");
        exit_status = EXIT_FAILURE;
        goto mxl_cleanup;
    }

    mxlFlowInfo audioFlowInfo;
    if (audioFlowID)
    {
        ret = mxlCreateFlowReader(instance, audioFlowID->c_str(), "", &audioReader);
        if (ret != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to create audio flow reader with status '{}'", static_cast<int>(ret));
            exit_status = EXIT_FAILURE;
            goto mxl_cleanup;
        }
        ret = mxlFlowReaderGetInfo(audioReader, &audioFlowInfo);
        if (ret != MXL_STATUS_OK)
        {
            MXL_ERROR("Failed to get audio flow info with status '{}'", static_cast<int>(ret));
            exit_status = EXIT_FAILURE;
            goto mxl_cleanup;
        }

        MXL_INFO("Audio flow info: rate={}/{}, channelCount={}",
            audioFlowInfo.continuous.sampleRate.numerator,
            audioFlowInfo.continuous.sampleRate.denominator,
            audioFlowInfo.continuous.channelCount);
    }

    gst_pipeline.start();

    headIndex = mxlGetCurrentIndex(&audioFlowInfo.continuous.sampleRate);
    while (!g_exit_requested)
    {
        if (audioReader)
        {
            mxlWrappedMultiBufferSlice audioPayload;
            auto ret = mxlFlowReaderGetSamples(audioReader, headIndex - sampleOffset, samplesPerBatch, &audioPayload);
            if (ret != MXL_STATUS_OK)
            {
                mxlFlowReaderGetInfo(audioReader, &audioFlowInfo);

                MXL_ERROR("Failing to get audio window -> {}:{} lastComittedIndex -> {}",
                    headIndex - (samplesPerBatch),
                    headIndex - (samplesPerBatch * 2),
                    audioFlowInfo.continuous.headIndex);

                // Fake missing data with silence. This is an error condition, so it is OK to allocate / free data here.
                auto const bufferLen = samplesPerBatch * 4;
                audioPayload.count = gst_config.audio_config->channelCount;
                audioPayload.stride = 0;
                auto payloadPtr = std::malloc(bufferLen);
                std::memset(payloadPtr, 0, bufferLen);
                audioPayload.base.fragments[0].pointer = payloadPtr;
                audioPayload.base.fragments[0].size = bufferLen;
                audioPayload.base.fragments[1].pointer = nullptr;
                audioPayload.base.fragments[1].size = 0;
                gst_pipeline.pushAudioSamples(audioPayload);
                std::free(payloadPtr);
            }
            else
            {
                gst_pipeline.pushAudioSamples(audioPayload);
            }

            headIndex += samplesPerBatch;
            mxlSleepForNs(mxlGetNsUntilIndex(headIndex, &audioFlowInfo.continuous.sampleRate));
        }
    }

mxl_cleanup:
    if (instance != nullptr)
    {
        // clean-up mxl objects
        if (audioReader)
        {
            mxlReleaseFlowReader(instance, audioReader);
        }
        mxlDestroyInstance(instance);
    }

    return exit_status;
}

int main(int argc, char* argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
    // macOS needs an NSApp event loop.  This gst function sets it up.
    return gst_macos_main((GstMainFunc)real_main, argc, argv, nullptr);
#else
    return real_main(argc, argv, nullptr);
#endif
}
