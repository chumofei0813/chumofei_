#ifndef ARMOR_DETECTOR__ARMORDETECTOR_H_
#define ARMOR_DETECTOR__ARMORDETECTOR_H_

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>

struct DetectorParams
{
  // Gamma 矫正
  double gamma = 1.0;

  // HSV 阈值 — 红色（两个区间取并集）
  int red_h_low1 = 0;
  int red_h_high1 = 30;
  int red_h_low2 = 170;
  int red_h_high2 = 180;
  int red_s_low = 50;
  int red_v_low = 30;

  // HSV 阈值 — 蓝色
  int blue_h_low = 70;
  int blue_h_high = 140;
  int blue_s_low = 120;
  int blue_v_low = 50;

  // 颜色 mask 形态学（先闭后开）
  int morph_color_close_size = 3;
  int morph_color_open_size = 1;

  // 过曝提取（V 通道辅助）
  bool use_overexpose = true;
  int overexpose_thresh = 200;

  // 最终 mask 形态学
  int morph_final_close_size = 1;
  int morph_final_open_size = 1;

  // 灯条筛选
  double light_area_min = 5.0;
  double light_area_max = 50000.0;
  double light_ratio_min = 2.0;
  double light_ratio_max = 20.0;
  double light_angle_max_diff = 30.0;
  double light_fill_ratio_min = 0.5;

  // 灯条配对
  double pair_ang_diff_max = 30.0;
  double pair_h_diff_max = 0.6;
  double pair_dy_ratio_max = 0.8;
  double pair_dx_ratio_min = 0.8;
  double pair_dx_ratio_max = 5.0;

  // 装甲板宽高比
  double armor_ratio_min = 0.7;
  double armor_ratio_max = 5.0;

  // 数字识别
  std::string classifier_model_path;
  double classifier_conf_thresh = 0.5;
  double classifier_vertical_extend_ratio = 0.5;
  double classifier_side_trim_ratio = 0.15;
};

struct ArmorResult
{
  enum Color { kRed = 0, kBlue = 1 };

  std::array < cv::Point2f, 4 > points;  // 左上, 右上, 右下, 左下
  int color = Color::kRed;
};

struct DebugInfo
{
  cv::Mat red_mask;
  cv::Mat blue_mask;
  std::vector < cv::RotatedRect > red_light_candidates;
  std::vector < cv::RotatedRect > blue_light_candidates;
  std::vector < std::pair < cv::RotatedRect, cv::RotatedRect >> paired_lights;
};

bool loadParamsFromYAML(const std::string & yaml_path, DetectorParams & params);

class ArmorDetector {
public:
  ArmorDetector() = default;

  void setParams(const DetectorParams & params);

  std::vector < ArmorResult > detect(
    const cv::Mat & bgr_frame,
    DebugInfo * debug = nullptr);

private:
  struct LightBar
  {
    cv::RotatedRect rect;
    cv::Point2f center;
    float angle = 0.0F;
    float height = 0.0F;
    float width = 0.0F;
    int color = 0;
  };

  DetectorParams params_;

  cv::Mat splitOverexpose(const cv::Mat & hsv) const;
  void getMasks(
    const cv::Mat & bgr, cv::Mat & red_mask,
    cv::Mat & blue_mask) const;
  void getEndpoints(
    const cv::RotatedRect & rect, cv::Point2f & top,
    cv::Point2f & bottom) const;
  std::vector < LightBar > extractLights(
    const cv::Mat & mask,
    int color_flag) const;
  std::vector < ArmorResult > pairLights(
    const std::vector < LightBar > &lights,
    DebugInfo * debug) const;
};

#endif  // ARMOR_DETECTOR__ARMORDETECTOR_H_
