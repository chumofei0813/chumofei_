#include <opencv2/opencv.hpp>
#include <iostream>
#include <cstring>
#include "armor_detector/ArmorDetector.h"
#include "armor_detector/NumberClassifier.h"
#include "ament_index_cpp/get_package_share_directory.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: image_detect <图片路径> [输出路径] [--config <yaml路径>]" << std::endl;
        std::cerr << "示例: image_detect test.jpg result.jpg" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = (argc >= 3 && strncmp(argv[2], "--", 2) != 0) ? argv[2] : "detect_result.jpg";
    std::string config_path;

    // 解析 --config 参数
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
            break;
        }
    }

    cv::Mat img = cv::imread(input_path);
    if (img.empty()) {
        std::cerr << "无法读取图片: " << input_path << std::endl;
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

    DebugInfo debug_info;
    auto results = detector.detect(img, &debug_info);

    // 数字识别（检测 + 分类合并在一次循环中，避免重复推理）
    std::vector<ClassifyResult> classify_results;
    std::cout << "检测到 " << results.size() << " 个装甲板:" << std::endl;
    for (size_t i = 0; i < results.size(); ++i) {
        const char* color_name = (results[i].color == 0) ? "红色" : "蓝色";
        cv::Mat roi = NumberClassifier::extractArmorROI(img, results[i].points);
        auto cls = classifier.classify(roi);
        classify_results.push_back(cls);

        std::cout << "  [" << i << "] " << color_name << "_" << cls.class_name
                  << " (置信度: " << cls.confidence << ")  四点坐标: ";
        for (int j = 0; j < 4; ++j)
            std::cout << "(" << results[i].points[j].x << "," << results[i].points[j].y << ") ";
        std::cout << std::endl;
    }

    // 画结果并保存
    cv::Mat output = img.clone();

    // 灯条
    for (const auto& r : debug_info.red_light_candidates) {
        cv::Point2f pts[4]; r.points(pts);
        for (int k = 0; k < 4; ++k) cv::line(output, pts[k], pts[(k+1)%4], cv::Scalar(0,100,255), 1);
    }
    for (const auto& r : debug_info.blue_light_candidates) {
        cv::Point2f pts[4]; r.points(pts);
        for (int k = 0; k < 4; ++k) cv::line(output, pts[k], pts[(k+1)%4], cv::Scalar(255,100,0), 1);
    }

    // 配对连线
    for (const auto& pair : debug_info.paired_lights) {
        cv::line(output, pair.first.center, pair.second.center, cv::Scalar(0, 255, 255), 1);
    }

    // 装甲板框 + 数字标注（复用已缓存的分类结果）
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& res = results[i];
        cv::Scalar color = (res.color == 0) ? cv::Scalar(0,0,255) : cv::Scalar(255,0,0);
        for (int k = 0; k < 4; ++k) cv::line(output, res.points[k], res.points[(k+1)%4], color, 2);

        std::string label = (res.color == 0 ? "RED_" : "BLUE_") + classify_results[i].class_name;
        cv::putText(output, label, res.points[0],
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
    }

    cv::imwrite(output_path, output);
    std::cout << "结果已保存到: " << output_path << std::endl;
    return 0;
}
