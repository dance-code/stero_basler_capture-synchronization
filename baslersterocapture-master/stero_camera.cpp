#include "stero_camera.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "json.hpp"

#include "stopwatch.h"
#include "utils.h"

using namespace Pylon;
using namespace Basler_UniversalCameraParams;
using namespace nlohmann;

namespace {
const bool kIoLow = true;
const bool kIoHigh = false;
}  // namespace

void PrintDeviceInfo(const CDeviceInfo& device);
std::pair<size_t, size_t> FindCameras(const std::string& left_camera_sn,
                                      const std::string& right_camera_sn,
                                      const DeviceInfoList_t& devices);
void TriggerPulse(CBaslerUniversalInstantCamera& camera);

void SteroCamera::Open(const std::string& stero_config_file_name) {
    std::ifstream stero_config_file(stero_config_file_name);
    json stero_config_json;
    stero_config_file >> stero_config_json;
    stero_config_file.close();
    Open(stero_config_json["left_camera"], stero_config_json["right_camera"],
         stero_config_json["frame_rate"]);
}

SteroCamera::SteroCamera()
        : grabbing_(false),
          left_grab_thread_stop_flag_(false),
          right_grab_thread_stop_flag_(false) {}

SteroCamera::~SteroCamera() {
    StopGrab();
}

void SteroCamera::Open(const std::string& left_camera_sn,
                       const std::string& right_camera_sn, double frame_rate) {
    CTlFactory& tl_factory = CTlFactory::GetInstance();
    DeviceInfoList_t devices;
    if (tl_factory.EnumerateDevices(devices) == 0) {
        throw std::runtime_error("相机未连接.");
    }

    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "相机 " << i << " 信息: " << std::endl;
        PrintDeviceInfo(devices[i]);
        std::cout << std::endl;
    }

    auto camera_index = FindCameras(left_camera_sn, right_camera_sn, devices);

    left_camera_.Attach(tl_factory.CreateDevice(devices[camera_index.first]));
    right_camera_.Attach(tl_factory.CreateDevice(devices[camera_index.second]));

    left_camera_.Open();
    right_camera_.Open();

    rate_.SetRate(frame_rate);
}

void SteroCamera::Init(const std::string& pylon_feature_stream_file) {
    CFeaturePersistence::Load(pylon_feature_stream_file.c_str(),
                              &left_camera_.GetNodeMap(), true);
    CFeaturePersistence::Load(pylon_feature_stream_file.c_str(),
                              &right_camera_.GetNodeMap(), true);

    left_camera_.GainAuto.SetValue(GainAuto_Continuous);
    right_camera_.GainAuto.SetValue(GainAuto_Off);

    left_camera_.ExposureAuto.SetValue(ExposureAuto_Continuous);
    right_camera_.ExposureAuto.SetValue(ExposureAuto_Off);

    left_camera_.BalanceWhiteAuto.SetValue(BalanceWhiteAuto_Continuous);
    right_camera_.BalanceWhiteAuto.SetValue(BalanceWhiteAuto_Off);

    left_camera_.UserOutputSelector.SetValue(UserOutputSelector_UserOutput3);
    left_camera_.UserOutputValue.SetValue(kIoLow);

    left_camera_.StartGrabbing();
    right_camera_.StartGrabbing();
}

void SteroCamera::StartGrab() {
    std::unique_lock<std::mutex> grabbing_lock(grabbing_mutex_);
    if (grabbing_) {
        return;
    }

    grabbing_ = true;

    StartLeftGrabThread();
    StartRightGrabThread();
}

void SteroCamera::StopGrab() {
    std::unique_lock<std::mutex> grabbing_lock(grabbing_mutex_);

    if (!grabbing_) {
        return;
    }

    right_grab_thread_stop_flag_ = true;
    right_grab_thread_.join();

    left_grab_thread_stop_flag_ = true;
    left_grab_thread_.join();

    grabbing_ = false;
}

std::pair<CGrabResultPtr, CGrabResultPtr> SteroCamera::Grab() {
    std::unique_lock<std::mutex> grabbing_lock(grabbing_mutex_);

    if (!grabbing_) {
        throw std::runtime_error("未开始采集");
    }

    CGrabResultPtr left_grab_result;
    CGrabResultPtr right_grab_result;

    std::unique_lock<std::mutex> lock(grab_result_queue_mutex_);
    if (left_grab_result_queue_.empty() || right_grab_result_queue_.empty()) {
        grab_result_queue_condition_variable_.wait(lock, [this]() {
            return !(left_grab_result_queue_.empty() ||
                     right_grab_result_queue_.empty());
        });
    }
    left_grab_result = left_grab_result_queue_.front();
    right_grab_result = right_grab_result_queue_.front();
    left_grab_result_queue_.pop_front();
    right_grab_result_queue_.pop_front();

    std::cout << TimeStr() << "获取左目图像编号: " << left_grab_result->GetBlockID()
              << " 获取右目图像编号: " << right_grab_result->GetBlockID() << std::endl;

    if (left_grab_result->GetBlockID() != right_grab_result->GetBlockID()) {
        throw std::runtime_error("左右获取到图像的编号不一致");
    }

    return std::make_pair(left_grab_result, right_grab_result);
}

double SteroCamera::GetFrameRate() const {
    return rate_.GetRate();
}

void SteroCamera::OnException(std::function<void(void)> callback) {
    exception_callback_ = callback;
}

void SteroCamera::SyncGain() {
    double gain = left_camera_.Gain.GetValue(false, true);
    right_camera_.Gain.SetValue(gain);
}

void SteroCamera::SyncExposureTime() {
    double exposure_time = left_camera_.ExposureTime.GetValue(false, true);
    right_camera_.ExposureTime.SetValue(exposure_time);
}

void SteroCamera::SyncWhiteBalance() {
    left_camera_.BalanceRatioSelector.SetValue(BalanceRatioSelector_Red);
    double red_ratio = left_camera_.BalanceRatio.GetValue(false, true);

    left_camera_.BalanceRatioSelector.SetValue(BalanceRatioSelector_Green);
    double green_ratio = left_camera_.BalanceRatio.GetValue(false, true);

    left_camera_.BalanceRatioSelector.SetValue(BalanceRatioSelector_Blue);
    double blue_ratio = left_camera_.BalanceRatio.GetValue(false, true);

    right_camera_.BalanceRatioSelector.SetValue(BalanceRatioSelector_Red);
    right_camera_.BalanceRatio.SetValue(red_ratio);

    right_camera_.BalanceRatioSelector.SetValue(BalanceRatioSelector_Green);
    right_camera_.BalanceRatio.SetValue(green_ratio);

    right_camera_.BalanceRatioSelector.SetValue(BalanceRatioSelector_Blue);
    right_camera_.BalanceRatio.SetValue(blue_ratio);
}

void SteroCamera::StartLeftGrabThread() {
    left_grab_thread_stop_flag_ = false;
    left_grab_thread_ = std::thread([this]() {
        try {
            rate_.Init();
            CGrabResultPtr left_grab_result;

            size_t trigger_count = 0;

            while (!left_grab_thread_stop_flag_) {

                SyncGain();
                SyncExposureTime();
                SyncWhiteBalance();
                left_camera_.WaitForFrameTriggerReady(
                        1000, TimeoutHandling_ThrowException);
                right_camera_.WaitForFrameTriggerReady(
                        1000, TimeoutHandling_ThrowException);

                TriggerPulse(left_camera_);

                left_camera_.RetrieveResult(5000, left_grab_result,
                                            TimeoutHandling_ThrowException);

                ++trigger_count;
                {
                    std::lock_guard<std::mutex> lock(grab_result_queue_mutex_);
                    left_grab_result_queue_.push_back(left_grab_result);
                }
                grab_result_queue_condition_variable_.notify_all();

                rate_.Sleep();
            }
        } catch (const Pylon::GenericException& e) {
            std::cerr << "发生相机异常(左): " << std::endl;
            std::cerr << e.GetDescription() << std::endl;
            if (exception_callback_) {
                exception_callback_();
            }
            std::cin.get();
            return;
        } catch (const std::runtime_error& e) {
            std::cerr << "发生运行异常: " << std::endl;
            std::cerr << e.what() << std::endl;
            if (exception_callback_) {
                exception_callback_();
            }
            std::cin.get();
            return;
        }
    });
}
void SteroCamera::StartRightGrabThread() {
    right_grab_thread_stop_flag_ = false;
    right_grab_thread_ = std::thread([this]() {
        try {
            CGrabResultPtr right_grab_result;
            while (!right_grab_thread_stop_flag_) {
                right_camera_.RetrieveResult(5000, right_grab_result,
                                             TimeoutHandling_ThrowException);

                {
                    std::lock_guard<std::mutex> lock(grab_result_queue_mutex_);
                    right_grab_result_queue_.push_back(right_grab_result);
                }
                grab_result_queue_condition_variable_.notify_all();
            }
        } catch (const Pylon::GenericException& e) {
            std::cerr << "发生相机异常(右): " << std::endl;
            std::cerr << e.GetDescription() << std::endl;
            if (exception_callback_) {
                exception_callback_();
            }
            std::cin.get();
            return;
        } catch (const std::runtime_error& e) {
            std::cerr << "发生运行异常: " << std::endl;
            std::cerr << e.what() << std::endl;
            if (exception_callback_) {
                exception_callback_();
            }
            std::cin.get();
            return;
        }
    });
}

void PrintDeviceInfo(const CDeviceInfo& device) {
    if (device.IsSerialNumberAvailable()) {
        std::cout << "SerialNumber: " << device.GetSerialNumber() << std::endl;
    }

    if (device.IsUserDefinedNameAvailable()) {
        std::cout << "UserDefinedName: " << device.GetUserDefinedName()
                  << std::endl;
    }

    if (device.IsModelNameAvailable()) {
        std::cout << "ModelName: " << device.GetModelName() << std::endl;
    }

    if (device.IsDeviceVersionAvailable()) {
        std::cout << "DeviceVersion: " << device.GetDeviceVersion()
                  << std::endl;
    }

    if (device.IsDeviceFactoryAvailable()) {
        std::cout << "DeviceFactory: " << device.GetDeviceFactory()
                  << std::endl;
    }

    if (device.IsInterfaceIDAvailable()) {
        std::cout << "InterfaceID: " << device.GetInterfaceID() << std::endl;
    }

    if (device.IsDeviceGUIDAvailable()) {
        std::cout << "DeviceGUID: " << device.GetDeviceGUID() << std::endl;
    }

    if (device.IsManufacturerInfoAvailable()) {
        std::cout << "ManufacturerInfo: " << device.GetManufacturerInfo()
                  << std::endl;
    }

    if (device.IsDeviceIdxAvailable()) {
        std::cout << "DeviceIdx: " << device.GetDeviceIdx() << std::endl;
    }

    if (device.IsProductIdAvailable()) {
        std::cout << "ProductId: " << device.GetProductId() << std::endl;
    }

    if (device.IsVendorIdAvailable()) {
        std::cout << "VendorId: " << device.GetVendorId() << std::endl;
    }

    if (device.IsDriverKeyNameAvailable()) {
        std::cout << "DriverKeyName: " << device.GetDriverKeyName()
                  << std::endl;
    }

    if (device.IsUsbDriverTypeAvailable()) {
        std::cout << "UsbDriverType: " << device.GetUsbDriverType()
                  << std::endl;
    }

    if (device.IsTransferModeAvailable()) {
        std::cout << "TransferMode: " << device.GetTransferMode() << std::endl;
    }
}

std::pair<size_t, size_t> FindCameras(const std::string& left_camera_sn,
                                      const std::string& right_camera_sn,
                                      const DeviceInfoList_t& devices) {
    int left_index = -1;
    int right_index = -1;

    for (int i = 0; i < devices.size(); ++i) {
        if (std::string(devices[i].GetSerialNumber().c_str()) ==
            left_camera_sn) {
            left_index = i;
        }

        if (std::string(devices[i].GetSerialNumber().c_str()) ==
            right_camera_sn) {
            right_index = i;
        }
    }

    if (left_index == -1) {
        throw std::runtime_error("左相机未连接");
    }

    if (right_index == -1) {
        throw std::runtime_error("右相机未连接");
    }

    return std::make_pair(static_cast<size_t>(left_index),
                          static_cast<size_t>(right_index));
}

void TriggerPulse(CBaslerUniversalInstantCamera& camera) {
    camera.UserOutputSelector.SetValue(UserOutputSelector_UserOutput3);
    camera.UserOutputValue.SetValue(kIoHigh);
    Sleep(1);
    camera.UserOutputValue.SetValue(kIoLow);
}