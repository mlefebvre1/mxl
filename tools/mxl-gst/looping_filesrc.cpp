// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <csignal>
#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
#include <rfl.hpp>
#include <uuid.h>
#include <CLI/CLI.hpp>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <rfl/json.hpp>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/FlowNMOS.hpp"
#include "mxl-internal/Logging.hpp"

namespace fs = std::filesystem;

std::sig_atomic_t volatile g_exit_requested = 0;

void signal_handler(int in_signal)
{
    switch (in_signal)
    {
        case SIGINT:  MXL_INFO("Received SIGINT, exiting..."); break;
        case SIGTERM: MXL_INFO("Received SIGTERM, exiting..."); break;
        default:      MXL_INFO("Received signal {}, exiting...", in_signal); break;
    }
    g_exit_requested = 1;
}

class LoopingFilePlayer
{
public:
    LoopingFilePlayer(std::string in_domain)
        : domain(std::move(in_domain))
    {
        // Create the MXL domain directory if it doesn't exist
        if (!fs::exists(domain))
        {
            try
            {
                fs::create_directories(domain);
                MXL_DEBUG("Created MXL domain directory: {}", domain);
            }
            catch (fs::filesystem_error const& e)
            {
                MXL_ERROR("Error creating domain directory: {}", e.what());
                throw;
            }
        }

        // Create the MXL SDK instance
        mxlInstance = mxlCreateInstance(domain.c_str(), nullptr);
        if (!mxlInstance)
        {
            throw std::runtime_error("Failed to create MXL instance");
        }
    }

    ~LoopingFilePlayer()
    {
        // Join threads if they were created
        if (videoThreadPtr && videoThreadPtr->joinable())
        {
            videoThreadPtr->join();
        }

        if (pipeline)
        {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }

        if (mxlInstance)
        {
            if (flowWriterVideo)
            {
                mxlReleaseFlowWriter(mxlInstance, flowWriterVideo);
                auto id = uuids::to_string(videoFlowId);
                mxlDestroyFlow(mxlInstance, id.c_str());
            }

            mxlDestroyInstance(mxlInstance);
        }
    }

    bool open(std::string const& in_uri)
    {
        uri = in_uri;
        MXL_DEBUG("Opening URI: {}", uri);

        //
        // Create the gstreamer pipeline
        //

        std::string pipelineDesc =
            "looping_filesrc location=" + in_uri +
            " ! tsdemux"
            " ! decodebin"
            " ! videorate"
            " ! videoconvert"
            " ! video/x-raw,format=v210,colorimetry=BT709"
            " ! queue"
            " ! appsink name=appSinkVideo"
            " emit-signals=false"
            " max-buffers=20"
            " drop=false"
            " sync=true";

        GError* error = nullptr;
        pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
        if (!pipeline || error)
        {
            MXL_ERROR("Failed to create pipeline: {}", error->message);
            g_error_free(error);
            return false;
        }

        auto bus = gst_element_get_bus(pipeline);

        gst_element_set_state(pipeline, GST_STATE_PAUSED);

        bool negotiated = false;
        while (!negotiated)
        {
            GstMessage* in_msg = gst_bus_timed_pop_filtered(
                bus, GST_CLOCK_TIME_NONE, static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

            if (!in_msg)
            {
                continue;
            }

            switch (GST_MESSAGE_TYPE(in_msg))
            {
                case GST_MESSAGE_ASYNC_DONE: negotiated = true; break;
                case GST_MESSAGE_ERROR:
                {
                    GError* err;
                    gchar* debug;
                    gst_message_parse_error(in_msg, &err, &debug);
                    MXL_ERROR("Pipeline error: {} ", err->message);
                    g_error_free(err);
                    g_free(debug);
                    break;
                }
                default: break;
            }
            gst_message_unref(in_msg);
        }
        gst_object_unref(bus);

        appSinkVideo = gst_bin_get_by_name(GST_BIN(pipeline), "appSinkVideo");

        if (!appSinkVideo)
        {
            MXL_ERROR("No video appsink found");
            return false;
        }

        if (appSinkVideo != nullptr)
        {
            MXL_DEBUG("Creating MXL flow for video...");

            // Get negotiated caps from appsink's pad
            GstPad* pad = gst_element_get_static_pad(appSinkVideo, "sink");
            GstCaps* caps = gst_pad_get_current_caps(pad);
            int width = 0, height = 0, fps_n = 0, fps_d = 1;
            gchar const* interlace_mode = nullptr;
            gchar const* colorimetry = nullptr;

            if (caps)
            {
                GstStructure* s = gst_caps_get_structure(caps, 0);
                interlace_mode = gst_structure_get_string(s, "interlace-mode");
                colorimetry = gst_structure_get_string(s, "colorimetry");

                gst_structure_get_int(s, "width", &width);
                gst_structure_get_int(s, "height", &height);

                if (width <= 0 || height <= 0)
                {
                    MXL_ERROR("Invalid width or height in caps");
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (!gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d))
                {
                    MXL_ERROR("Failed to get framerate from caps");
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (fps_n <= 0 || fps_d <= 0)
                {
                    MXL_ERROR("Invalid framerate in caps {}/{}", fps_n, fps_d);
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }
                else if (fps_n == 0 && fps_d == 1)
                {
                    MXL_ERROR("Invalid framerate in caps {}/{}.  This potentially signals that the video stream is VFR (variable frame rate) which "
                              "is unsupported by this application.",
                        fps_n,
                        fps_d);
                    gst_caps_unref(caps);
                    gst_object_unref(pad);
                    return false;
                }

                if (!interlace_mode)
                {
                    MXL_ERROR("Failed to get interlace mode from caps. Assuming progressive.");
                }

                if (!g_str_equal(interlace_mode, "progressive"))
                {
                    // TODO : Handle interlaced video
                    MXL_ERROR("Unsupported interlace mode.  Interpreting as progressive.");
                }

                // This assumes square pixels, bt709, sdr.  TODO read from caps.
                gst_caps_unref(caps);
            }
            else
            {
                MXL_ERROR("Failed to get caps from appsink pad");
                gst_object_unref(pad);
                return false;
            }

            gst_object_unref(pad);

            std::string flowDef;
            videoGrainRate = mxlRational{fps_n, fps_d};
            videoFlowId = createVideoFlowJson(uri, width, height, videoGrainRate, true, colorimetry, flowDef);

            mxlFlowInfo flowInfo;
            auto res = mxlCreateFlow(mxlInstance, flowDef.c_str(), nullptr, &flowInfo);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow: {}", (int)res);
                return false;
            }

            res = mxlCreateFlowWriter(mxlInstance, uuids::to_string(videoFlowId).c_str(), nullptr, &flowWriterVideo);
            if (res != MXL_STATUS_OK)
            {
                MXL_ERROR("Failed to create flow writer: {}", (int)res);
                return false;
            }

            MXL_INFO("Video flow : {}", uuids::to_string(videoFlowId));
        }

        return true;
    }

    bool start()
    {
        //
        // Start the pipeline
        //
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        running = true;

        //
        // Create the video thread to pull samples from the appsink
        //
        videoThreadPtr = appSinkVideo ? std::make_unique<std::thread>(&LoopingFilePlayer::videoThread, this) : nullptr;

        return true;
    }

    void stop()
    {
        running = false;
    }

    bool isRunning() const
    {
        return running;
    }

private:
    static uuids::uuid createVideoFlowJson(std::string const& in_uri, int in_width, int in_height, mxlRational in_rate, bool in_progressive,
        std::string const& in_colorspace, std::string& out_flowDef)
    {
        auto label = std::string{"Video flow for "} + in_uri;
        auto id = uuids::uuid_system_generator{}();

        auto nmosflow = mxl::lib::NMOSFlow::fromVideo(mxl::lib::NMOSVideoFlow{
            mxl::lib::NMOSCommonFlow{
                                     .description = label,
                                     .id = uuids::to_string(id),
                                     .tags = mxl::lib::NMOSCommonFlow::NMOSTags{.groupHints = {}},
                                     .label = label,
                                     .mediaType = "video/v210",
                                     },
            mxl::lib::Rational::fromMxl(in_rate),
            in_width,
            in_height,
            in_progressive ? std::string("progressive") : std::string("interlaced_tff"),
            in_colorspace,
            std::vector<mxl::lib::NMOSVideoFlow::Component>{
                                     mxl::lib::NMOSVideoFlow::Component{"Y", in_width, in_height, 10},
                                     mxl::lib::NMOSVideoFlow::Component{"Cb", in_width / 2, in_height, 10},
                                     mxl::lib::NMOSVideoFlow::Component{"Cr", in_width / 2, in_height, 10},
                                     }
        });

        out_flowDef = nmosflow.toJson();
        return id;
    }

    void videoThread()
    {
        while (running)
        {
            auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appSinkVideo), 100'000'000);
            if (sample)
            {
                uint64_t grainIndex = mxlGetCurrentIndex(&videoGrainRate);
                if (lastVideoGrainIndex == 0)
                {
                    lastVideoGrainIndex = grainIndex;
                }
                else if (grainIndex != lastVideoGrainIndex + 1)
                {
                    MXL_WARN("Video skipped grain index. Expected {}, got {}", lastVideoGrainIndex + 1, grainIndex);
                }

                lastVideoGrainIndex = grainIndex;

                auto buffer = gst_sample_get_buffer(sample);
                if (buffer)
                {
                    GstClockTime pts = GST_BUFFER_PTS(buffer);
                    if (GST_CLOCK_TIME_IS_VALID(pts))
                    {
                        [[maybe_unused]]
                        int64_t frame = currentFrame++;
                        MXL_TRACE("Video frame received.  Frame {}, pts (ms) {}, duration (ms) {}",
                            frame,
                            pts / GST_MSECOND,
                            GST_BUFFER_DURATION(buffer) / GST_MSECOND);
                    }
                }

                GstMapInfo map;
                if (gst_buffer_map(buffer, &map, GST_MAP_READ))
                {
                    /// Open the grain.
                    mxlGrainInfo gInfo;
                    uint8_t* mxl_buffer = nullptr;

                    /// Open the grain for writing.
                    if (mxlFlowWriterOpenGrain(flowWriterVideo, grainIndex, &gInfo, &mxl_buffer) != MXL_STATUS_OK)
                    {
                        MXL_ERROR("Failed to open grain at index '{}'", grainIndex);
                        break;
                    }

                    gInfo.validSlices = gInfo.totalSlices;
                    ::memcpy(mxl_buffer, map.data, map.size);

                    if (mxlFlowWriterCommitGrain(flowWriterVideo, &gInfo) != MXL_STATUS_OK)
                    {
                        MXL_ERROR("Failed to open grain at index '{}'", grainIndex);
                        break;
                    }

                    gst_buffer_unmap(buffer, &map);
                }

                auto ns = mxlGetNsUntilIndex(grainIndex, &videoGrainRate);
                gst_sample_unref(sample);
                mxlSleepForNs(ns);
            }
            else
            {
                MXL_WARN("No sample received while pulling from appsink");
            }
        }
    }

    // The URI GST PlayBin will use to play the video
    std::string uri;
    // The MXL video flow id
    uuids::uuid videoFlowId;
    // Unique pointer to video processing thread
    std::unique_ptr<std::thread> videoThreadPtr;
    // The MXL domain
    std::string domain;
    // Video flow writer allocated by the MXL instance
    ::mxlFlowWriter flowWriterVideo = nullptr;
    // The MXL instance
    ::mxlInstance mxlInstance = nullptr;

    ::GstElement* pipeline = nullptr;
    ::GstElement* appSinkVideo = nullptr;

    // Keep a copy of the last video grain index
    uint64_t lastVideoGrainIndex = 0;
    // Running flag
    std::atomic<bool> running{false};
    // Current frame number
    std::atomic<int64_t> currentFrame{0};
    // The video grain rate
    ::mxlRational videoGrainRate{0, 1};
};

int main(int argc, char* argv[])
{
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    //
    // Command line argument parsing
    //
    std::string inputFile, domain;

    CLI::App cli{"mxl-gst-looping-filesrc"};
    auto domainOpt = cli.add_option("-d,--domain", domain, "The MXL domain directory")->required();
    domainOpt->required(true);

    auto inputOpt = cli.add_option("-i,--input", inputFile, "MPEGTS media file location")->required();
    inputOpt->required(true);
    inputOpt->check(CLI::ExistingFile);

    CLI11_PARSE(cli, argc, argv);

    //
    // Initialize GStreamer
    //
    gst_init(&argc, &argv);

    // Simple scope guard to ensure GStreamer is de-initialized.
    // Replace with std::scope_exit when widely available ( C++23 )
    auto onExit = std::unique_ptr<void, void (*)(void*)>(nullptr, [](void*) { gst_deinit(); });

    //
    // Create the Player and open the input uri
    //
    auto player = std::make_unique<LoopingFilePlayer>(domain);
    if (!player->open(inputFile))
    {
        MXL_ERROR("Failed to open input file: {}", inputFile);
        return -1;
    }

    //
    // Start the player
    //
    if (!player->start())
    {
        MXL_ERROR("Failed to start the player");
        return -1;
    }

    while (!g_exit_requested && player->isRunning())
    {
        // Wait for the player to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (player->isRunning())
    {
        player->stop();
    }

    // Release the player
    player.reset();

    return 0;
}
