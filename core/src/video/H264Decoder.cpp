#include "H264Decoder.hpp"
#include "H264Sps.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <limits>
#include <stdexcept>

namespace Core::Video {
    namespace {
        struct PacketTag { double timestamp; std::uint32_t buffer; };
        void check(int result, const char *operation)
        {
            if (result < 0) {
                char error[AV_ERROR_MAX_STRING_SIZE]{};
                av_strerror(result, error, sizeof(error));
                throw std::runtime_error(std::string(operation) + ": " + error);
            }
        }
    }

    struct H264Decoder::Impl {
        AVCodecContext *context{};
        AVFrame *frame{};
        AVPacket *packet{};
        SwsContext *converter{};

        ~Impl()
        {
            sws_freeContext(converter);
            av_packet_free(&packet);
            av_frame_free(&frame);
            avcodec_free_context(&context);
        }

        std::vector<DecodedFrame> receive()
        {
            std::vector<DecodedFrame> result;
            for (;;) {
                const auto status = avcodec_receive_frame(context, frame);
                if (status == AVERROR(EAGAIN) || status == AVERROR_EOF)
                    break;
                check(status, "H264 receive");
                if (frame->width <= 0 || frame->height <= 0 || frame->width > 4096 || frame->height > 4096
                    || (frame->width & 1) || (frame->height & 1) || !frame->opaque_ref
                    || frame->opaque_ref->size != sizeof(PacketTag)) {
                    av_frame_unref(frame);
                    throw std::runtime_error("H264 invalid dimensions or missing packet ownership");
                }
                PacketTag tag{};
                std::memcpy(&tag, frame->opaque_ref->data, sizeof(tag));
                DecodedFrame output;
                output.width = frame->width;
                output.height = frame->height;
                output.stride = (output.width + 255u) & ~255u;
                output.cropTop = frame->crop_top;
                output.cropBottom = frame->crop_bottom;
                output.cropLeft = frame->crop_left;
                output.cropRight = frame->crop_right;
                output.timestamp = tag.timestamp;
                output.guestBuffer = tag.buffer;
                output.nv12.resize(std::size_t(output.stride) * output.height * 3 / 2);
                converter = sws_getCachedContext(converter, frame->width, frame->height,
                    static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                    AV_PIX_FMT_NV12, SWS_POINT, nullptr, nullptr, nullptr);
                if (!converter)
                    throw std::runtime_error("H264 NV12 converter allocation failed");
                std::uint8_t *planes[4]{output.nv12.data(), output.nv12.data() + output.stride * output.height};
                const int strides[4]{int(output.stride), int(output.stride), 0, 0};
                const auto rows = sws_scale(converter, frame->data, frame->linesize, 0, frame->height, planes, strides);
                if (rows != frame->height)
                    throw std::runtime_error("H264 incomplete NV12 conversion");
                av_frame_unref(frame);
                result.push_back(std::move(output));
            }
            return result;
        }
    };

    H264Decoder::H264Decoder() : impl(std::make_unique<Impl>())
    {
        const auto *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec)
            throw std::runtime_error("FFmpeg H264 decoder unavailable");
        impl->context = avcodec_alloc_context3(codec);
        impl->packet = av_packet_alloc();
        impl->frame = av_frame_alloc();
        if (!impl->context || !impl->packet || !impl->frame)
            throw std::bad_alloc();
        impl->context->thread_count = 1;
        impl->context->apply_cropping = 0;
        // FFmpeg carries this refcounted tag with the picture through B-frame reordering.
        impl->context->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
        check(avcodec_open2(impl->context, codec, nullptr), "H264 open");
    }

    H264Decoder::~H264Decoder() = default;

    void H264Decoder::reset() { avcodec_flush_buffers(impl->context); }

    std::vector<DecodedFrame> H264Decoder::decode(std::span<const std::uint8_t> bytes,
                                                 double timestamp, std::uint32_t guestBuffer)
    {
        if (bytes.empty() || bytes.size() > std::numeric_limits<int>::max())
            throw std::runtime_error("H264 invalid packet size");
        av_packet_unref(impl->packet);
        check(av_new_packet(impl->packet, static_cast<int>(bytes.size())), "H264 packet allocation");
        std::memcpy(impl->packet->data, bytes.data(), bytes.size());
        impl->packet->opaque_ref = av_buffer_alloc(sizeof(PacketTag));
        if (!impl->packet->opaque_ref)
            throw std::bad_alloc();
        const PacketTag tag{timestamp, guestBuffer};
        std::memcpy(impl->packet->opaque_ref->data, &tag, sizeof(tag));
        check(avcodec_send_packet(impl->context, impl->packet), "H264 send");
        av_packet_unref(impl->packet);
        return impl->receive();
    }

    std::vector<DecodedFrame> H264Decoder::drain()
    {
        const auto status = avcodec_send_packet(impl->context, nullptr);
        if (status != AVERROR_EOF)
            check(status, "H264 drain");
        return impl->receive();
    }

    bool H264Decoder::imageSize(std::span<const std::uint8_t> bytes, std::uint32_t &width, std::uint32_t &height)
    {
        return spsImageSize(bytes, width, height);
    }
}
