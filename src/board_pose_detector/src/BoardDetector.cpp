// BoardDetector 实现: 方框4角+圆心提取 + PnP 位姿解算
#include "board_pose_detector/BoardDetector.h"

#include <algorithm>
#include <cmath>

namespace board_pose_detector
{

void BoardDetector::setCameraInfo(
  const cv::Mat & camera_matrix, const cv::Mat & dist_coeffs)
{
  camera_matrix_ = camera_matrix.clone();
  dist_coeffs_ = dist_coeffs.clone();
}

// 3D 物体点: 圆心为原点(0,0,0)，板平面 Z=0，X 右 Y 下。
// 只用方框4角，顺序: 左上,右上,右下,左下。
// 圆心在正中≈4角形心，对位置无新信息但其像素误差会污染姿态解，故不参与 PnP，
// 仅用于确认是目标板 + 原点可视化。
std::vector<cv::Point3f> BoardDetector::buildObjectPoints() const
{
  const float h = static_cast<float>(params_.outer_size / 2.0);  // 半边长
  return {
    {-h, -h, 0.f},   // 左上
    { h, -h, 0.f},   // 右上
    { h,  h, 0.f},   // 右下
    {-h,  h, 0.f}    // 左下
  };
}

// 将 4 角按 左上,右上,右下,左下 排序:
// 先按 (x+y) 找左上(最小)/右下(最大)，再按 (x-y) 找右上(最大)/左下(最小)。
void BoardDetector::orderCorners(std::vector<cv::Point2f> & quad)
{
  CV_Assert(quad.size() == 4);
  std::vector<cv::Point2f> ordered(4);
  std::vector<float> sum(4), diff(4);
  for (int i = 0; i < 4; ++i) {
    sum[i] = quad[i].x + quad[i].y;
    diff[i] = quad[i].x - quad[i].y;
  }
  ordered[0] = quad[std::min_element(sum.begin(), sum.end()) - sum.begin()];   // 左上
  ordered[2] = quad[std::max_element(sum.begin(), sum.end()) - sum.begin()];   // 右下
  ordered[1] = quad[std::max_element(diff.begin(), diff.end()) - diff.begin()]; // 右上
  ordered[3] = quad[std::min_element(diff.begin(), diff.end()) - diff.begin()]; // 左下
  quad = ordered;
}

bool BoardDetector::findBoardQuad(
  const cv::Mat & binary, std::vector<cv::Point2f> & quad, double img_area)
{
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

  double best_area = 0.0;
  std::vector<cv::Point2f> best_quad;

  for (const auto & c : contours) {
    double area = cv::contourArea(c);
    if (area < params_.min_area_ratio * img_area ||
      area > params_.max_area_ratio * img_area)
    {
      continue;
    }

    // 多边形逼近
    std::vector<cv::Point> approx;
    double peri = cv::arcLength(c, true);
    cv::approxPolyDP(c, approx, params_.approx_epsilon_ratio * peri, true);

    // 只要凸四边形
    if (approx.size() != 4 || !cv::isContourConvex(approx)) {
      continue;
    }

    // 方正度检查: 最小外接矩形宽高比接近 1
    cv::RotatedRect rr = cv::minAreaRect(approx);
    double w = rr.size.width, h = rr.size.height;
    if (w < 1e-3 || h < 1e-3) {continue;}
    double squareness = std::min(w, h) / std::max(w, h);
    if (squareness < params_.min_quad_squareness) {continue;}

    // 取面积最大的合格四边形作为方框外轮廓
    if (area > best_area) {
      best_area = area;
      best_quad.assign(approx.begin(), approx.end());
    }
  }

  if (best_quad.size() != 4) {return false;}
  quad = best_quad;
  return true;
}

bool BoardDetector::findCircleCenter(
  const cv::Mat & binary, const std::vector<cv::Point2f> & quad,
  cv::Point2f & center)
{
  // 在方框内部找圆: 用方框四角构造掩膜，只在方框内部搜索圆轮廓
  cv::Point2f box_center(0.f, 0.f);
  for (const auto & p : quad) {box_center += p;}
  box_center *= 0.25f;

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

  double best_score = 0.0;
  bool found = false;
  for (const auto & c : contours) {
    double area = cv::contourArea(c);
    if (area < 20.0) {continue;}
    double peri = cv::arcLength(c, true);
    if (peri < 1e-3) {continue;}

    // 圆度 = 4πA / P²，圆接近 1
    double circularity = 4.0 * CV_PI * area / (peri * peri);
    if (circularity < params_.circle_min_circularity) {continue;}

    cv::Moments m = cv::moments(c);
    if (std::abs(m.m00) < 1e-6) {continue;}
    cv::Point2f cen(
      static_cast<float>(m.m10 / m.m00),
      static_cast<float>(m.m01 / m.m00));

    // 圆心应靠近方框中心
    double dist_to_box = cv::norm(cen - box_center);
    cv::RotatedRect rr = cv::minAreaRect(quad);
    double box_extent = std::max(rr.size.width, rr.size.height);
    if (dist_to_box > 0.3 * box_extent) {continue;}  // 离中心太远，排除

    // 打分: 越圆越靠中心越好
    double score = circularity - dist_to_box / box_extent;
    if (score > best_score) {
      best_score = score;
      center = cen;
      found = true;
    }
  }
  return found;
}

BoardResult BoardDetector::detect(const cv::Mat & bgr, cv::Mat * debug)
{
  BoardResult result;
  if (bgr.empty()) {return result;}
  if (debug) {*debug = bgr.clone();}

  // ---- 1. 灰度 + 二值化(方框/圆为黑，取反使目标为白) ----
  cv::Mat gray, binary;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
  int thresh_type = cv::THRESH_BINARY_INV;
  if (params_.use_otsu) {thresh_type |= cv::THRESH_OTSU;}
  cv::threshold(gray, binary, params_.binary_thresh, 255, thresh_type);

  // 形态学闭运算，连接断裂
  cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, {3, 3});
  cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

  double img_area = static_cast<double>(bgr.rows) * bgr.cols;

  // ---- 2. 找方框四角 ----
  std::vector<cv::Point2f> quad;
  if (!findBoardQuad(binary, quad, img_area)) {
    return result;  // 未找到目标板
  }
  orderCorners(quad);

  // ---- 3. 找圆心 ----
  cv::Point2f center;
  if (!findCircleCenter(binary, quad, center)) {
    return result;  // 有方框但无圆，判定不是目标板
  }

  result.corners = quad;
  result.center = center;

  // ---- 4. PnP 解算 ----
  if (!hasCameraInfo()) {
    result.found = true;  // 检测到但无内参，只给2D结果
    if (debug) {
      cv::putText(*debug, "NO CAMERA INFO", {20, 40},
        cv::FONT_HERSHEY_SIMPLEX, 1.0, {0, 0, 255}, 2);
    }
    return result;
  }

  std::vector<cv::Point3f> obj_pts = buildObjectPoints();
  std::vector<cv::Point2f> img_pts = quad;  // 仅方框4角，不含圆心

  cv::Vec3d rvec, tvec;
  bool ok = cv::solvePnP(
    obj_pts, img_pts, camera_matrix_, dist_coeffs_,
    rvec, tvec, false, cv::SOLVEPNP_ITERATIVE);
  if (!ok) {
    result.found = true;
    return result;
  }

  result.rvec = rvec;
  result.tvec = tvec;
  result.distance = cv::norm(tvec);

  // 欧拉角(度): 从旋转矩阵分解 (ZYX)
  cv::Mat R;
  cv::Rodrigues(rvec, R);
  double sy = std::sqrt(
    R.at<double>(0, 0) * R.at<double>(0, 0) +
    R.at<double>(1, 0) * R.at<double>(1, 0));
  bool singular = sy < 1e-6;
  double x, y, z;
  if (!singular) {
    x = std::atan2(R.at<double>(2, 1), R.at<double>(2, 2));
    y = std::atan2(-R.at<double>(2, 0), sy);
    z = std::atan2(R.at<double>(1, 0), R.at<double>(0, 0));
  } else {
    x = std::atan2(-R.at<double>(1, 2), R.at<double>(1, 1));
    y = std::atan2(-R.at<double>(2, 0), sy);
    z = 0;
  }
  result.roll = x * 180.0 / CV_PI;
  result.pitch = y * 180.0 / CV_PI;
  result.yaw = z * 180.0 / CV_PI;
  result.found = true;

  // ---- 5. 调试可视化 ----
  if (debug) {
    // 方框4角连线
    for (int i = 0; i < 4; ++i) {
      cv::line(*debug, quad[i], quad[(i + 1) % 4], {0, 255, 0}, 2);
      cv::circle(*debug, quad[i], 5, {0, 0, 255}, -1);
      cv::putText(*debug, std::to_string(i), quad[i] + cv::Point2f(8, 8),
        cv::FONT_HERSHEY_SIMPLEX, 0.6, {255, 255, 0}, 2);
    }
    // 圆心
    cv::circle(*debug, center, 6, {255, 0, 255}, -1);

    // 坐标轴(长度 = 半边长)，直观展示姿态
    cv::drawFrameAxes(
      *debug, camera_matrix_, dist_coeffs_, rvec, tvec,
      static_cast<float>(params_.outer_size / 2.0), 3);

    // 叠加距离与角度文字
    char buf[128];
    std::snprintf(
      buf, sizeof(buf), "dist=%.0fmm X=%.0f Y=%.0f Z=%.0f",
      result.distance, tvec[0], tvec[1], tvec[2]);
    cv::putText(*debug, buf, {20, 40},
      cv::FONT_HERSHEY_SIMPLEX, 0.7, {0, 255, 255}, 2);
    std::snprintf(
      buf, sizeof(buf), "roll=%.1f pitch=%.1f yaw=%.1f",
      result.roll, result.pitch, result.yaw);
    cv::putText(*debug, buf, {20, 70},
      cv::FONT_HERSHEY_SIMPLEX, 0.7, {0, 255, 255}, 2);
  }

  return result;
}

}  // namespace board_pose_detector
