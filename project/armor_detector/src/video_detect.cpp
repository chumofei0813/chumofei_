#include <opencv2/opencv.hpp>
#include <iostream>
#include <cstring>
#include "armor_detector/ArmorDetector.h"
#include "armor_detector/NumberClassifier.h"
#include "ament_index_cpp/get_package_share_directory.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: video_detect <视频路径> [输出路径] [--config <yaml路径>]" << std::endl;
        std::cerr << "示例: video_detect test.mp4 output.avi" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = (argc >= 3 && strncmp(argv[2], "--", 2) != 0) ? argv[2] : "detect_output.avi";
    std::string config_path;

    // 解析 --config 参数
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
            break;
        }
    }

    cv::VideoCapture cap(input_path);
    if (!cap.isOpened()) {
        std::cerr << "无法打开视频: " << input_path << std::endl;
        return 1;
    }

    ArmorDetector detector;
    DetectorParams params;

    // 如果未指定 --config，自动找包里的 armor_params.yaml
    if (config_path.empty()) {
        try {
            config_path = ament_index_cpp::get_package_share_directory("armor_detector")
                        + "/config/armor_params.yaml";
        } catch (...) {}
    }

    if (!config_path.empty()) {
        if (loadParamsFromYAML(config_path, params)) {
            std::cout << "已加载参数: " << config_path << std::endl;
        } else {
            std::cerr << "警告: 无法加载参数文件，使用默认参数" << std::endl;
        }
    }
    detector.setParams(params);

    // 初始化数字分类器
    std::string model_path = params.classifier_model_path;
    if (model_path.empty()) {
        try {
            model_path = ament_index_cpp::get_package_share_directory("armor_detector")
                       + "/model/tiny_resnet.onnx";
        } catch (...) {}
    }

    NumberClassifier classifier(model_path,
        static_cast<float>(params.classifier_conf_thresh));
    std::cout << "数字分类器已加载: " << model_path << std::endl;

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
        DebugInfo debug_info;
        auto results = detector.detect(frame, &debug_info);

        // 配对连线
        for (const auto& pair : debug_info.paired_lights) {
            cv::line(frame, pair.first.center, pair.second.center, cv::Scalar(0, 255, 255), 1);
        }

        // 装甲板框 + 数字标注
        for (const auto& res : results) {
            cv::Scalar color = (res.color == 0) ? cv::Scalar(0,0,255) : cv::Scalar(255,0,0);
            for (int k = 0; k < 4; ++k) cv::line(frame, res.points[k], res.points[(k+1)%4], color, 2);

            cv::Mat roi = NumberClassifier::extractArmorROI(frame, res.points);
            auto cls = classifier.classify(roi);
            std::string label = (res.color == 0 ? "RED_" : "BLUE_") + cls.class_name;
            cv::putText(frame, label, res.points[0],
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
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
