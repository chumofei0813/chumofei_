#include <cstring>

#include <iostream>
#include <string>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <opencv2/opencv.hpp>

#include "armor_detector/ArmorDetector.h"
#include "armor_detector/NumberClassifier.h"

int main(int argc, char ** argv)
{
  if (argc < 2) {
    std::cerr << "用法: image_detect <图片路径> [输出路径] "
      "[--config <yaml路径>]" << std::endl;
    std::cerr << "示例: image_detect test.jpg result.jpg" << std::endl;
    return 1;
  }

  std::string input_path = argv[1];
  std::string output_path =
    (argc >= 3 && std::strncmp(argv[2], "--", 2) != 0) ?
    argv[2] :
    "detect_result.jpg";
  std::string config_path;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
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

  if (config_path.empty()) {
    try {
      config_path =
        ament_index_cpp::get_package_share_directory("armor_detector") +
        "/config/armor_params.yaml";
    } catch (...) {
    }
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
      model_path =
        ament_index_cpp::get_package_share_directory("armor_detector") +
        "/model/tiny_resnet.onnx";
    } catch (...) {
    }
  }

  NumberClassifier classifier(
    model_path,
    static_cast<float>(params.classifier_conf_thresh),
    static_cast<float>(params.classifier_vertical_extend_ratio),
    static_cast<float>(params.classifier_side_trim_ratio));
  std::cout << "数字分类器已加载: " << model_path << std::endl;

  DebugInfo debug_info;
  auto results = detector.detect(img, &debug_info);

  // 分类
  std::vector<ClassifyResult> classify_results;
  std::cout << "检测到 " << results.size() << " 个装甲板:" << std::endl;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const char * color_name =
      (results[i].color == ArmorResult::kRed) ? "红色" : "蓝色";
    cv::Mat roi =
      classifier.extractArmorROI(img, results[i].points);
    auto cls = classifier.classify(roi);
    classify_results.push_back(cls);

    std::cout << "  [" << i << "] " << color_name << "_"
              << cls.class_name << " (置信度: " << cls.confidence
              << ")  四点坐标: ";
    for (const auto & pt : results[i].points) {
      std::cout << "(" << pt.x << "," << pt.y << ") ";
    }
    std::cout << std::endl;
  }

  // 绘图
  cv::Mat output = img.clone();

  auto draw_rotated_rect = [&](const cv::RotatedRect & r,
      const cv::Scalar & color) {
      cv::Point2f pts[4];
      r.points(pts);
      for (int k = 0; k < 4; ++k) {
        cv::line(output, pts[k], pts[(k + 1) % 4], color, 1);
      }
    };
  for (const auto & r : debug_info.red_light_candidates) {
    draw_rotated_rect(r, cv::Scalar(0, 100, 255));
  }
  for (const auto & r : debug_info.blue_light_candidates) {
    draw_rotated_rect(r, cv::Scalar(255, 100, 0));
  }

  for (const auto & pair : debug_info.paired_lights) {
    cv::line(
      output, pair.first.center, pair.second.center,
      cv::Scalar(0, 255, 255), 1);
  }

  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto & res = results[i];
    cv::Scalar color = (res.color == ArmorResult::kRed) ?
      cv::Scalar(0, 0, 255) :
      cv::Scalar(255, 0, 0);
    for (int k = 0; k < 4; ++k) {
      cv::line(output, res.points[k], res.points[(k + 1) % 4], color, 2);
    }

    std::string label =
      (res.color == ArmorResult::kRed ? "RED_" : "BLUE_") +
      classify_results[i].class_name;
    cv::putText(
      output, label, res.points[0],
      cv::FONT_HERSHEY_SIMPLEX, 0.5,
      cv::Scalar(255, 255, 255), 1);
  }

  cv::imwrite(output_path, output);
  std::cout << "结果已保存到: " << output_path << std::endl;
  return 0;
}
