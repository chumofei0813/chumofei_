#include <opencv2/opencv.hpp>
#include <iostream>
#include "armor_detector/ArmorDetector.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: image_detect <图片路径> [输出路径]" << std::endl;
        std::cerr << "示例: image_detect test.jpg result.jpg" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path = (argc >= 3) ? argv[2] : "detect_result.jpg";

    cv::Mat img = cv::imread(input_path);
    if (img.empty()) {
        std::cerr << "无法读取图片: " << input_path << std::endl;
        return 1;
    }

    ArmorDetector detector;
    DetectorParams params;
    detector.setParams(params);

    DebugInfo debug_info;
    auto results = detector.detect(img, &debug_info);

    std::cout << "检测到 " << results.size() << " 个装甲板:" << std::endl;
    for (size_t i = 0; i < results.size(); ++i) {
        const char* color_name = (results[i].color == 0) ? "红色" : "蓝色";
        std::cout << "  [" << i << "] " << color_name << "  四点坐标: ";
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

    // 装甲板框
    for (const auto& res : results) {
        cv::Scalar color = (res.color == 0) ? cv::Scalar(0,0,255) : cv::Scalar(255,0,0);
        for (int k = 0; k < 4; ++k) cv::line(output, res.points[k], res.points[(k+1)%4], color, 2);
        cv::putText(output, (res.color == 0 ? "RED" : "BLUE"),
                    res.points[0], cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
    }

    cv::imwrite(output_path, output);
    std::cout << "结果已保存到: " << output_path << std::endl;
    return 0;
}
