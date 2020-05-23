#ifndef STERO_CAMERA_H_
#define STERO_CAMERA_H_

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "date.h"

// Include files to use the pylon API.
#include <pylon/PylonIncludes.h>
#ifdef PYLON_WIN_BUILD
#include <pylon/PylonGUI.h>
#endif

// Settings to use any camera type.
#include <pylon/BaslerUniversalInstantCamera.h>

#include "rate.h"

class SteroCamera {
public:
    SteroCamera();
    ~SteroCamera();

public:
    void Open(const std::string& left_camera_sn,
              const std::string& right_camera_sn, double frame_rate);
    void Open(const std::string& stero_config_file_name);

    void Init(const std::string& pylon_feature_stream_file);

    void StartGrab();
    void StopGrab();

    std::pair<Pylon::CGrabResultPtr, Pylon::CGrabResultPtr> Grab();

    double GetFrameRate() const;

	void OnException(std::function<void(void)> callback);

private:
    void SyncGain();
    void SyncExposureTime();
    void SyncWhiteBalance();

private:
    void StartLeftGrabThread();
    void StartRightGrabThread();

private:
    enum class State { kTrigger, kGrab };

private:
    Rate rate_;

	std::function<void(void)> exception_callback_;

    Pylon::CBaslerUniversalInstantCamera left_camera_;
    Pylon::CBaslerUniversalInstantCamera right_camera_;

    std::atomic_bool grabbing_;
    std::mutex grabbing_mutex_;

    std::deque<Pylon::CGrabResultPtr> left_grab_result_queue_;
    std::deque<Pylon::CGrabResultPtr> right_grab_result_queue_;

    std::mutex grab_result_queue_mutex_;
    std::condition_variable grab_result_queue_condition_variable_;

    std::thread left_grab_thread_;
    std::atomic_bool left_grab_thread_stop_flag_;

    std::thread right_grab_thread_;
    std::atomic_bool right_grab_thread_stop_flag_;
};

#endif STERO_CAMERA_H_