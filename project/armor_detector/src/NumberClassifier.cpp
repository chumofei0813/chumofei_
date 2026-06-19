#include "armor_detector/NumberClassifier.h"

#include <algorithm>

NumberClassifier::NumberClassifier(
  const std::string & model_path,
  float conf_thresh,
  float vertical_extend_ratio,
  float side_trim_ratio)
: conf_thresh_(conf_thresh),
  vertical_extend_ratio_(vertical_extend_ratio),
  side_trim_ratio_(side_trim_ratio)
{
  net_ = cv::dnn::readNetFromONNX(model_path);
}

cv::Mat NumberClassifier::extractArmorROI(
  const cv::Mat & frame,
  const std::array<cv::Point2f, 4> & points) const
{
  // points 顺序: TL, TR, BR, BL（灯条端点）
  //
  // 裁剪策略：
  //   1. 沿装甲板竖直方向上下外扩角点
  //   2. 透视变换拉正
  //   3. 裁掉左右灯条区域，保留中间数字/图案

  // 装甲板竖直方向
  cv::Point2f down_dir = (points[3] - points[0]) +
    (points[2] - points[1]);
  float down_len = cv::norm(down_dir);
  if (down_len < kEpsilon) {return {};}
  down_dir /= down_len;
  cv::Point2f up_dir = -down_dir;

  // 沿竖直方向上下外扩
  float left_h = cv::norm(points[3] - points[0]);
  float right_h = cv::norm(points[2] - points[1]);
  float extend = (left_h + right_h) * 0.5F * vertical_extend_ratio_;

  std::array<cv::Point2f, 4> ext_pts;
  ext_pts[0] = points[0] + up_dir * extend;       // TL
  ext_pts[1] = points[1] + up_dir * extend;       // TR
  ext_pts[2] = points[2] + down_dir * extend;     // BR
  ext_pts[3] = points[3] + down_dir * extend;     // BL

  // 透视变换
  float top_w = cv::norm(ext_pts[1] - ext_pts[0]);
  float bottom_w = cv::norm(ext_pts[2] - ext_pts[3]);
  float ext_left_h = cv::norm(ext_pts[3] - ext_pts[0]);
  float ext_right_h = cv::norm(ext_pts[2] - ext_pts[1]);

  int dst_w = static_cast<int>(std::max(top_w, bottom_w));
  int dst_h = static_cast<int>(std::max(ext_left_h, ext_right_h));

  if (dst_w <= 0 || dst_h <= 0) {return {};}

  std::array<cv::Point2f, 4> dst_pts = {
    cv::Point2f(0.0F, 0.0F),
    cv::Point2f(static_cast<float>(dst_w - 1), 0.0F),
    cv::Point2f(
      static_cast<float>(dst_w - 1),
      static_cast<float>(dst_h - 1)),
    cv::Point2f(0.0F, static_cast<float>(dst_h - 1))
  };

  cv::Mat M = cv::getPerspectiveTransform(ext_pts.data(), dst_pts.data());
  cv::Mat roi;
  cv::warpPerspective(frame, roi, M, cv::Size(dst_w, dst_h));

  // 裁掉左右灯条区域
  int trim = static_cast<int>(dst_w * side_trim_ratio_);
  if (trim > 0 && trim * 2 < dst_w) {
    roi = roi(cv::Rect(trim, 0, dst_w - 2 * trim, dst_h));
  }

  return roi;
}

cv::Mat NumberClassifier::preprocess(const cv::Mat & gray_roi) const
{
  cv::Mat resized;
  cv::resize(
    gray_roi, resized,
    cv::Size(kModelInputSize, kModelInputSize),
    0, 0, cv::INTER_LINEAR);

  return cv::dnn::blobFromImage(
    resized, kNormalizeScale, cv::Size(),
    cv::Scalar(), false);
}

ClassifyResult NumberClassifier::classify(const cv::Mat & roi_bgr)
{
  ClassifyResult result;

  if (roi_bgr.empty()) {return result;}

  // BGR → Gray
  cv::Mat gray;
  if (roi_bgr.channels() == 3) {
    cv::cvtColor(roi_bgr, gray, cv::COLOR_BGR2GRAY);
  } else {
    gray = roi_bgr;
  }

  // 预处理为 (1, 1, 32, 32) blob
  cv::Mat blob = preprocess(gray);
  if (blob.empty()) {return result;}

  // 推理
  net_.setInput(blob);
  cv::Mat output = net_.forward();

  // 展平 + softmax
  cv::Mat flat;
  output.reshape(1, 1).convertTo(flat, CV_32F);

  double max_logit;
  cv::minMaxLoc(flat, nullptr, &max_logit);
  cv::Mat softmaxed;
  cv::exp(flat - max_logit, softmaxed);
  softmaxed /= cv::sum(softmaxed)[0];

  // argmax
  cv::Point max_loc;
  double max_val;
  cv::minMaxLoc(softmaxed, nullptr, &max_val, nullptr, &max_loc);

  result.class_id = max_loc.x;
  result.confidence = static_cast<float>(max_val);

  // 置信度低于阈值，视为不可靠
  if (result.confidence < conf_thresh_) {
    result.class_id = -1;
    result.class_name = "unknown";
    return result;
  }

  if (result.class_id >= 0 && result.class_id < kNumClasses) {
    result.class_name = kClassNames[result.class_id];
  } else {
    result.class_name = "unknown";
  }

  return result;
}
