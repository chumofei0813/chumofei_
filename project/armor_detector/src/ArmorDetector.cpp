#include "armor_detector/ArmorDetector.h"

#include <algorithm>
#include <cmath>
#include <fstream>

void ArmorDetector::setParams(const DetectorParams & params)
{
  params_ = params;
}

cv::Mat ArmorDetector::splitOverexpose(const cv::Mat & hsv) const
{
  std::vector<cv::Mat> channels;
  cv::split(hsv, channels);
  cv::Mat v_channel = channels[2];

  cv::Mat v_mask;
  cv::threshold(
    v_channel, v_mask, params_.overexpose_thresh, 255,
    cv::THRESH_BINARY);

  cv::Mat kernel_close = cv::getStructuringElement(
    cv::MORPH_RECT, cv::Size(5, 5));
  cv::morphologyEx(v_mask, v_mask, cv::MORPH_CLOSE, kernel_close);

  cv::Mat kernel_open = cv::getStructuringElement(
    cv::MORPH_RECT, cv::Size(2, 2));
  cv::morphologyEx(v_mask, v_mask, cv::MORPH_OPEN, kernel_open);

  return v_mask;
}

void ArmorDetector::getMasks(
  const cv::Mat & bgr, cv::Mat & red_mask,
  cv::Mat & blue_mask) const
{
  // Gamma 矫正
  cv::Mat gamma_corrected;
  bgr.convertTo(gamma_corrected, CV_32F, 1.0 / 255.0);
  cv::pow(gamma_corrected, params_.gamma, gamma_corrected);
  gamma_corrected.convertTo(gamma_corrected, CV_8U, 255.0);

  cv::Mat hsv;
  cv::cvtColor(gamma_corrected, hsv, cv::COLOR_BGR2HSV);

  // 红色 mask（H 通道红色分布在两端，两个区间取并集）
  cv::Mat red1, red2;
  cv::inRange(
    hsv,
    cv::Scalar(
      params_.red_h_low1, params_.red_s_low,
      params_.red_v_low),
    cv::Scalar(params_.red_h_high1, 255, 255), red1);
  cv::inRange(
    hsv,
    cv::Scalar(
      params_.red_h_low2, params_.red_s_low,
      params_.red_v_low),
    cv::Scalar(params_.red_h_high2, 255, 255), red2);
  cv::Mat red_color = red1 | red2;

  // 蓝色 mask
  cv::Mat blue_color;
  cv::inRange(
    hsv,
    cv::Scalar(
      params_.blue_h_low, params_.blue_s_low,
      params_.blue_v_low),
    cv::Scalar(params_.blue_h_high, 255, 255), blue_color);

  // 颜色 mask 形态学：先闭后开，消除孔洞和噪点
  cv::Mat kernel_color_close = cv::getStructuringElement(
    cv::MORPH_RECT,
    cv::Size(params_.morph_color_close_size, params_.morph_color_close_size));
  cv::morphologyEx(red_color, red_color, cv::MORPH_CLOSE, kernel_color_close);
  cv::morphologyEx(
    blue_color, blue_color, cv::MORPH_CLOSE,
    kernel_color_close);

  cv::Mat kernel_color_open = cv::getStructuringElement(
    cv::MORPH_RECT,
    cv::Size(params_.morph_color_open_size, params_.morph_color_open_size));
  cv::morphologyEx(red_color, red_color, cv::MORPH_OPEN, kernel_color_open);
  cv::morphologyEx(blue_color, blue_color, cv::MORPH_OPEN, kernel_color_open);

  // 过曝提取（可选，利用 V 通道辅助过滤高亮区域）
  if (params_.use_overexpose) {
    cv::Mat v_mask = splitOverexpose(hsv);
    red_mask = red_color & v_mask;
    blue_mask = blue_color & v_mask;
  } else {
    red_mask = red_color;
    blue_mask = blue_color;
  }

  // 最终 mask 形态学
  cv::Mat kernel_final_close = cv::getStructuringElement(
    cv::MORPH_RECT,
    cv::Size(params_.morph_final_close_size, params_.morph_final_close_size));
  cv::morphologyEx(red_mask, red_mask, cv::MORPH_CLOSE, kernel_final_close);
  cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_CLOSE, kernel_final_close);

  cv::Mat kernel_final_open = cv::getStructuringElement(
    cv::MORPH_RECT,
    cv::Size(params_.morph_final_open_size, params_.morph_final_open_size));
  cv::morphologyEx(red_mask, red_mask, cv::MORPH_OPEN, kernel_final_open);
  cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_OPEN, kernel_final_open);
}

void ArmorDetector::getEndpoints(
  const cv::RotatedRect & rect,
  cv::Point2f & top,
  cv::Point2f & bottom) const
{
  cv::Point2f pts[4];
  rect.points(pts);
  std::sort(
    std::begin(pts), std::end(pts),
    [](const cv::Point2f & a, const cv::Point2f & b) {
      return a.y < b.y;
    });
  top = (pts[0] + pts[1]) * 0.5F;
  bottom = (pts[2] + pts[3]) * 0.5F;
}

std::vector<ArmorDetector::LightBar> ArmorDetector::extractLights(
  const cv::Mat & mask, int color_flag) const
{
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(
    mask, contours, cv::RETR_EXTERNAL,
    cv::CHAIN_APPROX_SIMPLE);

  std::vector<LightBar> lights;
  for (const auto & cnt : contours) {
    // 面积筛选
    double area = cv::contourArea(cnt);
    if (area < params_.light_area_min || area > params_.light_area_max) {
      continue;
    }

    cv::RotatedRect r = cv::minAreaRect(cnt);
    float w = r.size.width;
    float h = r.size.height;
    float angle = r.angle;

    // 保证 h 是长边（高度），w 是短边（宽度）
    if (w > h) {
      std::swap(w, h);
    }

    // 长宽比筛选
    float ratio = h / w;
    if (ratio < params_.light_ratio_min || ratio > params_.light_ratio_max) {
      continue;
    }

    // 矩形填充率筛选
    float rect_area = w * h;
    float fill_ratio = static_cast<float>(area) / rect_area;
    if (fill_ratio < params_.light_fill_ratio_min) {continue;}

    // 角度归一化（配对依赖，不能注释）
    if (r.size.width < r.size.height) {
      angle += 90.0F;
    }
    if (angle < 0.0F) {
      angle += 180.0F;
    }
    if (angle > 90.0F) {
      angle = 180.0F - angle;
    }

    // 角度筛选（过滤非竖直灯条，可单独开关）
    if (std::abs(angle - 90.0F) > params_.light_angle_max_diff) {
      continue;
    }

    LightBar lb;
    lb.rect = r;
    lb.center = r.center;
    lb.angle = angle;
    lb.height = h;
    lb.width = w;
    lb.color = color_flag;
    lights.push_back(lb);
  }
  return lights;
}

std::vector<ArmorResult> ArmorDetector::pairLights(
  const std::vector<LightBar> & lights, DebugInfo * debug) const
{
  std::vector<ArmorResult> armors;

  for (std::size_t i = 0; i < lights.size(); ++i) {
    for (std::size_t j = i + 1; j < lights.size(); ++j) {
      const LightBar & l1 = lights[i];
      const LightBar & l2 = lights[j];
      if (l1.color != l2.color) {continue;}

      // 配对筛选
      float ang_diff = std::abs(l1.angle - l2.angle);
      float h_diff = std::abs(l1.height - l2.height) /
        std::max(l1.height, l2.height);
      float avg_len = (l1.height + l2.height) * 0.5F;
      float dx = std::abs(l1.center.x - l2.center.x);
      float dy = std::abs(l1.center.y - l2.center.y);
      float dx_ratio = dx / avg_len;
      float dy_ratio = dy / avg_len;

      if (ang_diff > params_.pair_ang_diff_max) {continue;}
      if (h_diff > params_.pair_h_diff_max) {continue;}
      if (dy_ratio > params_.pair_dy_ratio_max) {continue;}
      if (dx_ratio < params_.pair_dx_ratio_min ||
        dx_ratio > params_.pair_dx_ratio_max)
      {
        continue;
      }

      // 确定左右灯条
      const LightBar * left =
        (l1.center.x < l2.center.x) ? &l1 : &l2;
      const LightBar * right =
        (l1.center.x < l2.center.x) ? &l2 : &l1;

      // 提取端点
      cv::Point2f left_top, left_bottom, right_top, right_bottom;
      getEndpoints(left->rect, left_top, left_bottom);
      getEndpoints(right->rect, right_top, right_bottom);

      // 装甲板宽高比筛选
      float armor_width = cv::norm(right_top - left_top);
      float left_height = cv::norm(left_bottom - left_top);
      float right_height = cv::norm(right_bottom - right_top);
      float armor_height = (left_height + right_height) * 0.5F;
      if (armor_height > 0.0F &&
        (armor_width / armor_height < params_.armor_ratio_min ||
        armor_width / armor_height > params_.armor_ratio_max))
      {
        continue;
      }

      ArmorResult res;
      res.points = {left_top, right_top, right_bottom, left_bottom};
      res.color = l1.color;
      armors.push_back(res);

      if (debug) {
        debug->paired_lights.push_back(
          {left->rect, right->rect});
      }
    }
  }
  return armors;
}

std::vector<ArmorResult> ArmorDetector::detect(
  const cv::Mat & bgr_frame,
  DebugInfo * debug)
{
  if (bgr_frame.empty()) {
    return {};
  }

  cv::Mat red_mask, blue_mask;
  getMasks(bgr_frame, red_mask, blue_mask);

  if (debug) {
    debug->red_mask = red_mask.clone();
    debug->blue_mask = blue_mask.clone();
  }

  std::vector<LightBar> red_lights = extractLights(red_mask, 0);
  std::vector<LightBar> blue_lights = extractLights(blue_mask, 1);
  std::vector<LightBar> all_lights;
  all_lights.reserve(red_lights.size() + blue_lights.size());
  all_lights.insert(all_lights.end(), red_lights.begin(), red_lights.end());
  all_lights.insert(all_lights.end(), blue_lights.begin(), blue_lights.end());

  if (debug) {
    for (const auto & l : red_lights) {
      debug->red_light_candidates.push_back(l.rect);
    }
    for (const auto & l : blue_lights) {
      debug->blue_light_candidates.push_back(l.rect);
    }
  }

  return pairLights(all_lights, debug);
}

// ============================================================================
// YAML 参数加载（独立于 ArmorDetector 类，供离线工具复用）
// ============================================================================

bool loadParamsFromYAML(const std::string & yaml_path, DetectorParams & params)
{
  std::ifstream file(yaml_path);
  if (!file.is_open()) {return false;}

  std::string line;
  bool in_params = false;

  while (std::getline(file, line)) {
    std::size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
      continue;                                    // 空行
    }
    if (line[start] == '#') {
      continue;                                     // 注释
    }
    // 等待进入 ros__parameters 区块
    if (!in_params) {
      if (line.find("ros__parameters") != std::string::npos) {
        in_params = true;
      }
      continue;
    }

    // 离开 ros__parameters 区块
    if (start < 4) {break;}

    // 解析 key: value
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) {continue;}

    std::string key = line.substr(start, colon - start);
    while (!key.empty() && key.back() == ' ') {key.pop_back();}

    std::string val_str = line.substr(colon + 1);
    std::size_t val_start = val_str.find_first_not_of(" \t");
    if (val_start == std::string::npos) {continue;}
    std::size_t val_end = val_str.find_first_of(" \t#\r", val_start);
    val_str = val_str.substr(val_start, val_end - val_start);

    if (val_str.empty()) {continue;}

    try {
      bool vb;
      if (val_str == "true" || val_str == "True") {
        vb = true;
      } else if (val_str == "false" || val_str == "False") {
        vb = false;
      } else {
        vb = (std::stod(val_str) != 0.0);
      }

      double v = vb ? 1.0 : 0.0;
      if (val_str != "true" && val_str != "True" &&
        val_str != "false" && val_str != "False")
      {
        v = std::stod(val_str);
      }
      int vi = static_cast<int>(v);

      if (key == "gamma") {params.gamma = v;} else if (key == "red_h_low1") {
        params.red_h_low1 = vi;
      } else if (key == "red_h_high1") {params.red_h_high1 = vi;} else if (key == "red_h_low2") {
        params.red_h_low2 = vi;
      } else if (key == "red_h_high2") {params.red_h_high2 = vi;} else if (key == "red_s_low") {
        params.red_s_low = vi;
      } else if (key == "red_v_low") {params.red_v_low = vi;} else if (key == "blue_h_low") {
        params.blue_h_low = vi;
      } else if (key == "blue_h_high") {params.blue_h_high = vi;} else if (key == "blue_s_low") {
        params.blue_s_low = vi;
      } else if (key == "blue_v_low") {
        params.blue_v_low = vi;
      } else if (key == "morph_color_close_size") {
        params.morph_color_close_size = vi;
      } else if (key == "morph_color_open_size") {
        params.morph_color_open_size = vi;
      } else if (key == "use_overexpose") {
        params.use_overexpose = vb;
      } else if (key == "overexpose_thresh") {
        params.overexpose_thresh = vi;
      } else if (key == "morph_final_close_size") {
        params.morph_final_close_size = vi;
      } else if (key == "morph_final_open_size") {
        params.morph_final_open_size = vi;
      } else if (key == "light_area_min") {
        params.light_area_min = v;
      } else if (key == "light_area_max") {
        params.light_area_max = v;
      } else if (key == "light_ratio_min") {
        params.light_ratio_min = v;
      } else if (key == "light_ratio_max") {
        params.light_ratio_max = v;
      } else if (key == "light_angle_max_diff") {
        params.light_angle_max_diff = v;
      } else if (key == "light_fill_ratio_min") {
        params.light_fill_ratio_min = v;
      } else if (key == "pair_ang_diff_max") {
        params.pair_ang_diff_max = v;
      } else if (key == "pair_h_diff_max") {
        params.pair_h_diff_max = v;
      } else if (key == "pair_dy_ratio_max") {
        params.pair_dy_ratio_max = v;
      } else if (key == "pair_dx_ratio_min") {
        params.pair_dx_ratio_min = v;
      } else if (key == "pair_dx_ratio_max") {
        params.pair_dx_ratio_max = v;
      } else if (key == "armor_ratio_min") {
        params.armor_ratio_min = v;
      } else if (key == "armor_ratio_max") {
        params.armor_ratio_max = v;
      } else if (key == "model_path") {
        params.classifier_model_path = val_str;
      } else if (key == "classify_conf_thresh") {
        params.classifier_conf_thresh = v;
      } else if (key == "classify_vertical_extend_ratio") {
        params.classifier_vertical_extend_ratio = v;
      } else if (key == "classify_side_trim_ratio") {
        params.classifier_side_trim_ratio = v;
      }
    } catch (...) {
      continue;
    }
  }

  return true;
}
