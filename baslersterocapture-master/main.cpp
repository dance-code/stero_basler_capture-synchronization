#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Include files to use the pylon API.
#include <pylon/PylonIncludes.h>
#ifdef PYLON_WIN_BUILD
#include <pylon/PylonGUI.h>
#endif

#include <opencv2/opencv.hpp>

#include "json.hpp"

#include "rate.h"
#include "stero_camera.h"
#include "video_recorder.h"

#include "utils.h"

nlohmann::json GetVideoConfig(const std::string& config_file_name);

int main() {
    Pylon::PylonInitialize();

    SteroCamera stero_camera;

    cv::Mat left_image, right_image;
    cv::Mat combine_image;

    cv::namedWindow("Basler", cv::WINDOW_KEEPRATIO);

    VideoRecorder video_recorder;

    std::string time_str = TimeStrLocal();
    std::replace(time_str.begin(), time_str.end(), ':', '-');
    std::string file_name = "Basler" + time_str + ".avi";
    std::cout << file_name << std::endl;

    try {
        stero_camera.Open("stero_config.json");
        stero_camera.Init("camera.pfs");

        auto video_cofig = GetVideoConfig("video_config.json");
        video_recorder.Open(file_name, 3840, 1080, stero_camera.GetFrameRate(),
                            video_cofig["bit_rate"]);

        stero_camera.OnException([&]() { video_recorder.Close(); });
        stero_camera.StartGrab();

        while (true) {
            auto grab_results = stero_camera.Grab();

            auto& left_result = grab_results.first;
            auto& right_result = grab_results.second;

            left_image =
                    cv::Mat(left_result->GetHeight(), left_result->GetWidth(),
                            CV_8UC3, left_result->GetBuffer());
            right_image =
                    cv::Mat(right_result->GetHeight(), right_result->GetWidth(),
                            CV_8UC3, right_result->GetBuffer());

            cv::hconcat(left_image, right_image, combine_image);

            video_recorder.Write(combine_image);

            cv::imshow("Basler", combine_image);
            cv::resizeWindow("Basler", cv::Size(1280, 360));

            auto c = cv::waitKey(1);

            if (c == 27 || c == 'q' || c == 'Q') {
                stero_camera.StopGrab();
                std::cout << "已停止采集图像" << std::endl;
                video_recorder.Close();
                break;
            }
        }

        // std::cin.get();
        exit(0);
    } catch (const Pylon::GenericException& e) {
        std::cerr << "发生相机异常: " << std::endl;
        std::cerr << e.GetDescription() << std::endl;
        video_recorder.Close();
        std::cin.get();
        exit(-1);
    } catch (const std::runtime_error& e) {
        std::cerr << "发生运行异常: " << std::endl;
        std::cerr << e.what() << std::endl;
        video_recorder.Close();
        std::cin.get();
        exit(-1);
    }
    return 0;
}

nlohmann::json GetVideoConfig(const std::string& config_file_name) {
    std::ifstream file(config_file_name);
    nlohmann::json config_json;
    file >> config_json;
    file.close();
    return config_json;
}