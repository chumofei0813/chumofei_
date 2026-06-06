#include <opencv2/opencv.hpp>
#include <iostream>
#include "armor_detector/ArmorDetector.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: video_detect <视频路径> [输出路径]" << std::endl;
        std::cerr << "示例: video_detect test.mp4 output.avi" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = (argc >= 3) ? argv[2] : "detect_output.avi";

    cv::VideoCapture cap(input_path);
    if (!cap.isOpened()) {
        std::cerr << "无法打开视频: " << input_path << std::endl;
        return 1;
    }

    ArmorDetector detector;
    DetectorParams params;
    detector.setParams(params);

    int fps = cap.get(cv::CAP_PROP_FPS);
    int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    int total = cap.get(cv::CAP_PROP_FRAME_COUNT);

    cv::VideoWriter writer(output_path,
        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
        fps > 0 ? fps : 30,
        cv::Size(width, height));

    if (!writer.isOpened()) {
        std::cerr << "无法创建输出视频: " << output_path << std::endl;
        return 1;
    }

    cv::Mat frame;
    int frame_idx = 0;
    while (cap.read(frame)) {
        auto results = detector.detect(frame);
        for (const auto& res : results) {
            cv::Scalar color = (res.color == 0) ? cv::Scalar(0,0,255) : cv::Scalar(255,0,0);
            for (int k = 0; k < 4; ++k) cv::line(frame, res.points[k], res.points[(k+1)%4], color, 2);
        }
        writer.write(frame);

        if (++frame_idx % 30 == 0)
            std::cout << "\r处理进度: " << frame_idx << "/" << total << " 帧" << std::flush;
    }
    cap.release();
    writer.release();
    std::cout << "\r处理完成: " << frame_idx << " 帧, 保存到 " << output_path << std::endl;
    return 0;
}
