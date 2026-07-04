#ifndef ARMOR_DETECTOR__NUMBERCLASSIFIER_H_
#define ARMOR_DETECTOR__NUMBERCLASSIFIER_H_

#include <array>
#include <string>

#include <opencv2/opencv.hpp>

/// 数字分类结果
struct ClassifyResult
{
  int class_id = -1;          // 0~8，-1 表示推理失败
  float confidence = 0.0F;
  std::string class_name;     // "one", "two", ..., "not_armor"
};

/// 基于 ONNX 的装甲板数字/图案分类器
class NumberClassifier {
public:
  static constexpr int kNumClasses = 9;
  static constexpr int kModelInputSize = 32;
  static constexpr float kNormalizeScale = 1.0F / 255.0F;

  // 类别映射：0=one, 1=two, ..., 7=base, 8=not_armor
  static constexpr const char * kClassNames[kNumClasses] = {
    "one", "two", "three", "four", "five",
    "sentry", "outpost", "base", "not_armor"
  };

  /// @param model_path  ONNX 模型文件路径
  /// @param conf_thresh 置信度阈值，低于此值返回 unknown
  /// @param vertical_extend_ratio 竖直方向外扩比例
  /// @param side_trim_ratio 水平方向灯条裁剪比例
  explicit NumberClassifier(
    const std::string & model_path,
    float conf_thresh = 0.5F,
    float vertical_extend_ratio = 0.5F,
    float side_trim_ratio = 0.15F);

  /// 对装甲板 ROI 做数字分类
  /// @param roi_bgr 裁剪后的装甲板 BGR 图像
  ClassifyResult classify(const cv::Mat & roi_bgr);

  /// 从原图中提取装甲板 ROI
  /// @param points 顺序：左上, 右上, 右下, 左下
  cv::Mat extractArmorROI(
    const cv::Mat & frame,
    const std::array < cv::Point2f, 4 > & points) const;

private:
  static constexpr float kEpsilon = 1e-6F;

  cv::dnn::Net net_;
  float conf_thresh_;
  float vertical_extend_ratio_;
  float side_trim_ratio_;

  /// 灰度 → 拉伸至 kModelInputSize×kModelInputSize → [0,1] 归一化 → NCHW blob
  cv::Mat preprocess(const cv::Mat & gray_roi) const;
};

#endif  // ARMOR_DETECTOR__NUMBERCLASSIFIER_H_
