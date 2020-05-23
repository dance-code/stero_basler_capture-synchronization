#ifndef VIDEO_RECORDER_H_
#define VIDEO_RECORDER_H_

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

public:
    void Open(const std::string& name, size_t width, size_t height, double fps,
              int64_t bit_rate);
    void Close();

    void Write(const cv::Mat& image);

private:
    void Init(const std::string& name, size_t width, size_t height, double fps,
              int64_t bit_rate);
    void Encode(const cv::Mat& image);
    void EncodeAVFrame(AVCodecContext* codec_context, AVFrame* frame,
                       AVPacket* packet);

private:
    bool is_opened_;

    std::deque<cv::Mat> image_queue_;
    std::mutex image_queue_mutex_;
    std::condition_variable image_queue_condition_variable_;

    std::thread writer_thread_;
    std::atomic_bool writer_thread_stop_flag_;

    // cv::VideoWriter writer_;

    AVOutputFormat* format_;
    AVFormatContext* format_context_;
    AVCodec* codec_;
    AVCodecContext* codec_context_;

    AVStream* stream_;
    AVFrame* frame_;
    AVPacket* packet_;
    SwsContext* sws_context_;

    size_t frame_count_;
};

#endif