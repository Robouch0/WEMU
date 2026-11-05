#include "audio/MovieAudio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace Core::Audio {
    struct MovieAudio::Impl {
            AVFormatContext *format{};
            AVCodecContext *codec{};
            AVPacket *packet{};
            AVFrame *frame{};
            SwrContext *resampler{};
            int stream{-1};
            bool draining{}, eof{};
            double origin{};
            std::int64_t first{}, next{};
            std::vector<float> block;
            ~Impl()
            {
                swr_free(&resampler);
                av_frame_free(&frame);
                av_packet_free(&packet);
                avcodec_free_context(&codec);
                avformat_close_input(&format);
            }
            void seek(std::int64_t sample)
            {
                // AAC and other overlapping codecs need packets before the
                // target, including negative priming timestamps at time zero.
                const auto *audio = format->streams[stream];
                const double timestamp = (static_cast<double>(sample) / 48000.0 - .1 + origin) / av_q2d(audio->time_base);
                if (!std::isfinite(timestamp) || timestamp < double(INT64_MIN) || timestamp >= double(INT64_MAX) ||
                    av_seek_frame(format, stream, std::int64_t(timestamp), AVSEEK_FLAG_BACKWARD) < 0)
                    throw std::runtime_error("Movie audio seek failed");
                avcodec_flush_buffers(codec);
                swr_close(resampler);
                if (swr_init(resampler) < 0)
                    throw std::runtime_error("Movie audio resampler reset failed");
                draining = eof = false;
                block.clear();
                first = next = sample;
            }
            bool read()
            {
                block.clear();
                while (!eof) {
                    const auto result = avcodec_receive_frame(codec, frame);
                    if (result == 0 || result == AVERROR_EOF) {
                        const bool flush = result == AVERROR_EOF;
                        if (!flush && (frame->nb_samples < 0 || frame->nb_samples > 192000 || frame->sample_rate != codec->sample_rate))
                            throw std::runtime_error("Unsupported movie audio frame");
                        const auto delay = swr_get_delay(resampler, 48000);
                        first = !flush && frame->best_effort_timestamp != AV_NOPTS_VALUE
                                        ? std::llround(
                                                  (static_cast<double>(frame->best_effort_timestamp) * av_q2d(format->streams[stream]->time_base) -
                                                   origin) *
                                                  48000) -
                                                  delay
                                        : next;
                        const int capacity = swr_get_out_samples(resampler, flush ? 0 : frame->nb_samples);
                        if (capacity < 0 || capacity > 480000)
                            throw std::runtime_error("Invalid movie audio conversion size");
                        block.resize(std::size_t(capacity) * 2);
                        auto *destination = reinterpret_cast<std::uint8_t *>(block.data());
                        const int count =
                                swr_convert(resampler, &destination, capacity,
                                            flush ? nullptr : const_cast<const std::uint8_t **>(frame->extended_data), flush ? 0 : frame->nb_samples);
                        av_frame_unref(frame);
                        if (count < 0)
                            throw std::runtime_error("Movie audio conversion failed");
                        block.resize(std::size_t(count) * 2);
                        next = first + count;
                        if (flush && count == 0)
                            eof = true;
                        if (count)
                            return true;
                        continue;
                    }
                    if (result != AVERROR(EAGAIN) || draining)
                        throw std::runtime_error("Movie audio decoder failed");
                    int status;
                    do {
                        av_packet_unref(packet);
                        status = av_read_frame(format, packet);
                    } while (status >= 0 && packet->stream_index != stream);
                    if (status < 0 && status != AVERROR_EOF)
                        throw std::runtime_error("Movie audio demux failed");
                    draining = status == AVERROR_EOF;
                    const auto sent = avcodec_send_packet(codec, draining ? nullptr : packet);
                    av_packet_unref(packet);
                    if (sent < 0)
                        throw std::runtime_error("Movie audio packet rejected");
                }
                return false;
            }
    };
    MovieAudio::MovieAudio(const std::string &path) : impl(std::make_unique<Impl>())
    {
        if (avformat_open_input(&impl->format, path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(impl->format, nullptr) < 0)
            throw std::runtime_error("Cannot open movie audio");
        const AVCodec *decoder{};
        impl->stream = av_find_best_stream(impl->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (impl->stream < 0 || !decoder)
            return; // A video-only movie is valid.
        const int video = av_find_best_stream(impl->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (video >= 0 && impl->format->streams[video]->start_time != AV_NOPTS_VALUE)
            impl->origin = static_cast<double>(impl->format->streams[video]->start_time) * av_q2d(impl->format->streams[video]->time_base);
        else if (impl->format->start_time != AV_NOPTS_VALUE)
            impl->origin = double(impl->format->start_time) / AV_TIME_BASE;
        impl->codec = avcodec_alloc_context3(decoder);
        if (!impl->codec || avcodec_parameters_to_context(impl->codec, impl->format->streams[impl->stream]->codecpar) < 0 ||
            avcodec_open2(impl->codec, decoder, nullptr) < 0 || impl->codec->sample_rate <= 0)
            throw std::runtime_error("Cannot initialize movie audio decoder");
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&impl->resampler, &stereo, AV_SAMPLE_FMT_FLT, 48000, &impl->codec->ch_layout, impl->codec->sample_fmt,
                                impl->codec->sample_rate, 0, nullptr) < 0 ||
            swr_init(impl->resampler) < 0)
            throw std::runtime_error("Cannot initialize movie audio resampler");
        impl->packet = av_packet_alloc();
        impl->frame = av_frame_alloc();
        if (!impl->packet || !impl->frame)
            throw std::runtime_error("Cannot allocate movie audio buffers");
    }
    MovieAudio::~MovieAudio() = default;
    bool MovieAudio::available() const { return impl->codec != nullptr; }
    void MovieAudio::mix(double seconds, std::span<float> stereo)
    {
        if (!available())
            return;
        if (!std::isfinite(seconds) || seconds < 0 || seconds > double(INT64_MAX / 48000) || stereo.size() % 2)
            throw std::runtime_error("Invalid movie audio position");
        const auto target = std::llround(seconds * 48000);
        if (target < impl->first || target > impl->next + 12000)
            impl->seek(target);
        for (std::size_t i = 0; i < stereo.size() / 2; ++i) {
            const auto position = target + std::int64_t(i);
            while (position >= impl->next && !impl->eof)
                if (!impl->read())
                    break;
            if (position < impl->first || position >= impl->next)
                continue;
            const auto index = std::size_t(position - impl->first) * 2;
            stereo[i * 2] += impl->block[index] * 32768;
            stereo[i * 2 + 1] += impl->block[index + 1] * 32768;
        }
    }
} // namespace Core::Audio
