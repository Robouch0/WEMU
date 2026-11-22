#include "video/MovieDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace Core::Video {
    struct MovieDecoder::Impl {
            AVFormatContext *format{};
            AVCodecContext *codec{};
            AVPacket *packet{};
            AVFrame *decoded{};
            SwsContext *scale{};
            int stream{-1};
            bool draining{};
            std::uint64_t serial{};
            std::shared_ptr<MovieFrame> current, next;
            ~Impl()
            {
                sws_freeContext(scale);
                av_frame_free(&decoded);
                av_packet_free(&packet);
                avcodec_free_context(&codec);
                avformat_close_input(&format);
            }
            std::shared_ptr<MovieFrame> read()
            {
                while (true) {
                    const auto result = avcodec_receive_frame(codec, decoded);
                    if (result == AVERROR_EOF)
                        return {};
                    if (result == 0) {
                        if (decoded->width <= 0 || decoded->height <= 0 || decoded->width > 4096 || decoded->height > 4096)
                            throw std::runtime_error("Unsupported movie dimensions");
                        auto frame = std::make_shared<MovieFrame>();
                        frame->width = decoded->width;
                        frame->height = decoded->height;
                        frame->timestamp =
                                decoded->best_effort_timestamp == AV_NOPTS_VALUE
                                        ? 0
                                        : double(decoded->best_effort_timestamp -
                                                 (format->streams[stream]->start_time == AV_NOPTS_VALUE ? 0 : format->streams[stream]->start_time)) *
                                                  av_q2d(format->streams[stream]->time_base);
                        frame->serial = ++serial;
                        frame->rgba.resize(std::size_t(frame->width) * frame->height * 4);
                        scale = sws_getCachedContext(scale, decoded->width, decoded->height, AVPixelFormat(decoded->format), decoded->width,
                                                     decoded->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                        if (!scale)
                            throw std::runtime_error("Cannot convert movie frame");
                        const auto *coefficients = sws_getCoefficients(decoded->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT);
                        sws_setColorspaceDetails(scale, coefficients, decoded->color_range == AVCOL_RANGE_JPEG, coefficients, 1, 0, 1 << 16, 1 << 16);
                        std::uint8_t *planes[]{frame->rgba.data()};
                        const int strides[]{int(frame->width * 4)};
                        sws_scale(scale, decoded->data, decoded->linesize, 0, decoded->height, planes, strides);
                        av_frame_unref(decoded);
                        return frame;
                    }
                    if (result != AVERROR(EAGAIN) || draining)
                        throw std::runtime_error("Movie decoder failed while receiving a frame");
                    int status;
                    do {
                        av_packet_unref(packet);
                        status = av_read_frame(format, packet);
                    } while (status >= 0 && packet->stream_index != stream);
                    if (status < 0 && status != AVERROR_EOF)
                        throw std::runtime_error("Movie demux failed");
                    draining = status == AVERROR_EOF;
                    const auto sent = avcodec_send_packet(codec, draining ? nullptr : packet);
                    av_packet_unref(packet);
                    if (sent < 0)
                        throw std::runtime_error("Movie decoder rejected a packet");
                }
            }
    };

    MovieDecoder::MovieDecoder(const std::string &path) : impl(std::make_unique<Impl>())
    {
        if (avformat_open_input(&impl->format, path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(impl->format, nullptr) < 0)
            throw std::runtime_error("Cannot open movie file");
        const AVCodec *decoder{};
        impl->stream = av_find_best_stream(impl->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if (impl->stream < 0 || !decoder)
            throw std::runtime_error("Movie has no supported video stream");
        impl->codec = avcodec_alloc_context3(decoder);
        if (!impl->codec || avcodec_parameters_to_context(impl->codec, impl->format->streams[impl->stream]->codecpar) < 0)
            throw std::runtime_error("Cannot allocate movie codec");
        // Small menu clips do not need frame-thread latency or a host thread per core.
        impl->codec->thread_count = 2;
        if (avcodec_open2(impl->codec, decoder, nullptr) < 0)
            throw std::runtime_error("Cannot initialize movie codec");
        impl->packet = av_packet_alloc();
        impl->decoded = av_frame_alloc();
        if (!impl->packet || !impl->decoded)
            throw std::runtime_error("Cannot allocate movie buffers");
    }
    MovieDecoder::~MovieDecoder() = default;
    void MovieDecoder::seek(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::runtime_error("Invalid movie seek position");
        const auto *stream = impl->format->streams[impl->stream];
        const long double ticks =
                static_cast<long double>(seconds) / av_q2d(stream->time_base) + (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time);
        if (!std::isfinite(ticks) || ticks < std::numeric_limits<std::int64_t>::min() || ticks > std::numeric_limits<std::int64_t>::max())
            throw std::runtime_error("Movie seek position exceeds timestamp range");
        const auto target = static_cast<std::int64_t>(ticks);
        if (av_seek_frame(impl->format, impl->stream, target, AVSEEK_FLAG_BACKWARD) < 0)
            throw std::runtime_error("Movie seek failed");
        avcodec_flush_buffers(impl->codec);
        impl->draining = false;
        impl->current.reset();
        impl->next.reset();
    }
    std::shared_ptr<const MovieFrame> MovieDecoder::frameAt(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::runtime_error("Invalid movie playback position");
        if (impl->current && seconds < impl->current->timestamp)
            seek(seconds);
        if (!impl->next)
            impl->next = impl->read();
        while (impl->next && (impl->next->timestamp <= seconds + 1.e-9 || !impl->current)) {
            impl->current = std::move(impl->next);
            impl->next = impl->read();
        }
        return impl->current;
    }
    double MovieDecoder::duration() const { return impl->format->duration == AV_NOPTS_VALUE ? 0 : double(impl->format->duration) / AV_TIME_BASE; }
    unsigned MovieDecoder::width() const { return impl->codec->width; }
    unsigned MovieDecoder::height() const { return impl->codec->height; }
} // namespace Core::Video
