#include "video_recorder.h"

#include <iostream>

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")

namespace {
const char* kCodecName = "mpeg4";
const AVPixelFormat kPixelFormat = AV_PIX_FMT_YUV420P;
//const AVPixelFormat kPixelFormat = AV_PIX_FMT_YUV422P;

std::string GetErrorString(int error_num) {
    char av_error[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, error_num);
    return std::string(av_error);
}

}  // namespace

VideoRecorder::VideoRecorder()
        : is_opened_(false),
          writer_thread_stop_flag_(false),
          format_(nullptr),
          format_context_(nullptr),
          codec_(nullptr),
          codec_context_(nullptr),
          stream_(nullptr),
          frame_(nullptr),
          packet_(nullptr),
          sws_context_(nullptr),
          frame_count_(0) {}

VideoRecorder::~VideoRecorder() {
    Close();
}

void VideoRecorder::Open(const std::string& name, size_t width, size_t height,
                         double fps, int64_t bit_rate) {
    if (is_opened_) {
        return;
    }

    is_opened_ = true;
    writer_thread_stop_flag_ = false;

    /*writer_.open(name, cv::VideoWriter::fourcc('D', 'I', 'V', 'X'), fps,
                 cv::Size(static_cast<int>(width), static_cast<int>(height)),
                 true);*/
    Init(name, width, height, fps, bit_rate);

    writer_thread_ = std::thread([this]() {
        size_t count = 0;
        try {
            std::cout << "开始录制" << std::endl;
            while (!writer_thread_stop_flag_) {
                cv::Mat image;
                {
                    std::unique_lock<std::mutex> lock(image_queue_mutex_);
                    if (image_queue_.empty()) {
                        image_queue_condition_variable_.wait(lock, [this]() {
                            return !image_queue_.empty() ||
                                   writer_thread_stop_flag_;
                        });
                    }
                    if (writer_thread_stop_flag_) {
                        break;
                    }
                    image = image_queue_.front();
                    image_queue_.pop_front();
                }
                Encode(image);
                ++count;
                std::cout << "写入第 " << count << " 帧" << std::endl;
            }
            std::unique_lock<std::mutex> lock(image_queue_mutex_);
            while (!image_queue_.empty()) {
                std::cout << "正在写入视频，还剩: " << image_queue_.size()
                          << " 帧" << std::endl;
                Encode(image_queue_.front());
                image_queue_.pop_front();
            }
        } catch (const std::exception& e) {
            std::cout << "发生视频写入异常: " << e.what() << std::endl;
            exit(-1);
        }
    });
}

void VideoRecorder::Close() {
    if (!is_opened_) {
        return;
    }
    writer_thread_stop_flag_ = true;
    image_queue_condition_variable_.notify_all();
    writer_thread_.join();
    /*writer_.release();*/
    EncodeAVFrame(codec_context_, nullptr, packet_);

    av_write_trailer(format_context_);
    avio_close(format_context_->pb);

    avcodec_close(codec_context_);
    avcodec_free_context(&codec_context_);
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    sws_freeContext(sws_context_);
    avformat_free_context(format_context_);

    format_ = nullptr;
    format_context_ = nullptr;
    codec_ = nullptr;
    codec_context_ = nullptr;
    frame_ = nullptr;
    packet_ = nullptr;
    sws_context_ = nullptr;

	std::cout << "停止录制，视频已关闭" << std::endl;
}

void VideoRecorder::Write(const cv::Mat& image) {
    if (!is_opened_) {
        return;
    }
    {
        std::unique_lock<std::mutex> lock(image_queue_mutex_);
        image_queue_.push_back(image);
    }
    image_queue_condition_variable_.notify_all();
}

void VideoRecorder::Init(const std::string& name, size_t width, size_t height,
                         double fps, int64_t bit_rate) {
    format_ = av_guess_format(nullptr, name.c_str(), nullptr);
    if (!format_) {
        throw std::runtime_error("无法找到封装格式");
    }

    avformat_alloc_output_context2(&format_context_, format_, format_->name,
                                   name.c_str());
    if (!format_context_) {
        throw std::runtime_error("无法创建封装器");
    }

    stream_ = avformat_new_stream(format_context_, nullptr);
    if (!stream_) {
        throw std::runtime_error("无法创建视频流");
    }

    codec_ = avcodec_find_encoder_by_name(kCodecName);
    if (!codec_) {
        throw std::runtime_error("无法找到编码器");
    }

    codec_context_ = avcodec_alloc_context3(codec_);
    if (!codec_context_) {
        throw std::runtime_error("无法初始化编码器");
    }

    codec_context_->codec_id = codec_->id;
    codec_context_->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_context_->bit_rate = bit_rate;
    codec_context_->width = static_cast<int>(width);
    codec_context_->height = static_cast<int>(height);
    codec_context_->time_base = AVRational{1, static_cast<int>(fps * 1)};
    codec_context_->gop_size = 1;
    codec_context_->max_b_frames = 0;
    codec_context_->qmin = 1;
    codec_context_->qmax = 1;
    codec_context_->pix_fmt = kPixelFormat;

    if (codec_->id == AV_CODEC_ID_MPEG2VIDEO) {
        codec_context_->max_b_frames = 2;
    }

    if (codec_->id == AV_CODEC_ID_MPEG1VIDEO) {
        codec_context_->mb_decision = 2;
    }

    if (format_context_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (codec_->id == AV_CODEC_ID_H264) {
        av_opt_set(codec_context_->priv_data, "preset", "slow", 0);
    }

    int ret = avcodec_open2(codec_context_, codec_, nullptr);
    if (ret < 0) {
        throw std::runtime_error("无法打开编码器: " + GetErrorString(ret));
    }

    avcodec_parameters_from_context(stream_->codecpar, codec_context_);

    av_dump_format(format_context_, 0, name.c_str(), 1);

    stream_->time_base = codec_context_->time_base =
            AVRational{1, static_cast<int>(fps * 1)};
    stream_->r_frame_rate = stream_->avg_frame_rate =
            AVRational{static_cast<int>(fps), 1};

    ret = avio_open(&format_context_->pb, name.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        throw std::runtime_error("无法打开文件: " + name);
    }

    avformat_write_header(format_context_, nullptr);

    int buffer_size = av_image_get_buffer_size(codec_context_->pix_fmt,
                                               codec_context_->width,
                                               codec_context_->height, 1);

    frame_ = av_frame_alloc();
    if (!frame_) {
        throw std::runtime_error("无法初始化视频帧");
    }

    frame_->format = codec_context_->pix_fmt;
    frame_->width = codec_context_->width;
    frame_->height = codec_context_->height;

    ret = av_frame_get_buffer(frame_, 32);
    if (ret < 0) {
        throw std::runtime_error("无法分配视频帧空间");
    }

    packet_ = av_packet_alloc();
    if (!packet_) {
        throw std::runtime_error("无法初始化视频数据包");
    }

    sws_context_ = sws_getContext(
            codec_context_->width, codec_context_->height, AV_PIX_FMT_BGR24,
            codec_context_->width, codec_context_->height,
            codec_context_->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!sws_context_) {
        throw std::runtime_error("无法初始化帧格式转换");
    }

    frame_count_ = 0;
}

void VideoRecorder::Encode(const cv::Mat& image) {
    /*writer_ << image;*/
    int ret = av_frame_make_writable(frame_);
    if (ret < 0) {
        throw std::runtime_error("准备写入视频帧错误");
    }

    int cv_line_sizes[1];
    cv_line_sizes[0] = static_cast<int>(image.step1());

    sws_scale(sws_context_, &(image.data), cv_line_sizes, 0, image.rows,
              frame_->data, frame_->linesize);

    frame_->pts = frame_count_;
    EncodeAVFrame(codec_context_, frame_, packet_);

    ++frame_count_;
}

void VideoRecorder::EncodeAVFrame(AVCodecContext* codec_context, AVFrame* frame,
                                  AVPacket* packet) {
    int ret = avcodec_send_frame(codec_context, frame);
    if (ret < 0) {
        throw std::runtime_error("发送视频帧到编码器出错");
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            throw std::runtime_error("编码视频帧出错");
        }
        packet->stream_index = stream_->index;
        av_write_frame(format_context_, packet_);
        av_packet_unref(packet);
    }
}