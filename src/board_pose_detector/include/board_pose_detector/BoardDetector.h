// BoardDetector: 目标板识别 + PnP 位姿解算（不依赖 ROS，只依赖 OpenCV）
// ============================================================================
// 目标板: 黑色方框 + 中心黑圆(居中)。
//   - 关键点: 方框外轮廓 4 角 + 圆心，共 5 点
//   - 3D 模型: 以圆心为原点，板平面为 Z=0，X 右 Y 下 Z 射出
//   - 解算: cv::solvePnP (SOLVEPNP_IPPE_SQUARE, 平面正方形专用)
// 输出: 旋转向量 rvec + 平移向量 tvec (单位 mm)，及调试可视化信息。
// ============================================================================
#ifndef BOARD_POSE_DETECTOR__BOARDDETECTOR_H_
#define BOARD_POSE_DETECTOR__BOARDDETECTOR_H_

#include <opencv2/opencv.hpp>
#include <vector>

namespace board_pose_detector
{

// 检测/解算可调参数
struct BoardParams
{
  // ---- 目标板物理尺寸(mm)，来源: 尺子实测 ----
  double outer_size = 150.0;   // 黑色方框外边长
  double inner_size = 115.0;   // 白色区域内边长
  double circle_diameter = 50.0;  // 中心圆直径

  // ---- 二值化 ----
  int binary_thresh = 80;      // 灰度阈值(方框/圆为黑, 低于阈值判黑)
  bool use_otsu = true;        // 是否用 Otsu 自动阈值(优先于 binary_thresh)

  // ---- 方框轮廓筛选 ----
  double min_area_ratio = 0.005;  // 方框最小面积占画面比例
  double max_area_ratio = 0.9;    // 方框最大面积占画面比例
  double approx_epsilon_ratio = 0.02;  // approxPolyDP 精度(周长比例)
  double min_quad_squareness = 0.6;    // 四边形"方正度"下限(宽/高接近1)

  // ---- 圆筛选 ----
  double circle_min_circularity = 0.7;  // 圆度下限 4πA/P²

  // ---- 圆/方框尺寸比例校验(抗误检) ----
  // 真目标板: 圆直径/外框边长 = 50/150 ≈ 0.333。检测到的圆直径与方框边长之比
  // 需落在 [circle_ratio_min, circle_ratio_max]，否则判为误检(如背景黑方块)。
  double circle_ratio_min = 0.22;  // 圆直径/方框边长 下限
  double circle_ratio_max = 0.48;  // 圆直径/方框边长 上限

  // ---- 重投影误差筛选(抗跳解/误检) ----
  // solvePnP 后将4角3D点投影回图像，与实测角点的平均像素误差超过此值则丢弃该帧。
  double max_reproj_error = 6.0;   // 最大平均重投影误差(像素)
};

// 单次检测的结果
struct BoardResult
{
  bool found = false;

  std::vector<cv::Point2f> corners;  // 方框外4角(左上,右上,右下,左下)
  cv::Point2f center;                // 圆心像素坐标

  cv::Vec3d rvec;   // 旋转向量(相机系)
  cv::Vec3d tvec;   // 平移向量(mm, 相机系): 目标板中心位置

  // 便于阅读的派生量(节点层可直接用于打印/叠字)
  double distance = 0.0;              // 到目标板中心的直线距离(mm)
  double roll = 0.0, pitch = 0.0, yaw = 0.0;  // 欧拉角(度)
};

class BoardDetector
{
public:
  BoardDetector() = default;

  void setParams(const BoardParams & p) {params_ = p;}
  const BoardParams & params() const {return params_;}

  // 设置相机内参(K:3x3, dist:1x5)。必须在 detect 前调用一次。
  void setCameraInfo(const cv::Mat & camera_matrix, const cv::Mat & dist_coeffs);
  bool hasCameraInfo() const {return !camera_matrix_.empty();}

  // 主入口: 输入 BGR 图，输出检测+解算结果。
  // debug 非空时，在其上绘制检测框/关键点/坐标轴等可视化。
  BoardResult detect(const cv::Mat & bgr, cv::Mat * debug = nullptr);

private:
  BoardParams params_;
  cv::Mat camera_matrix_;   // K (3x3, CV_64F)
  cv::Mat dist_coeffs_;     // D (1x5, CV_64F)

  // 从二值图中找到目标板方框(最外层四边形)，成功返回 true 并填 4 角(未排序)
  bool findBoardQuad(const cv::Mat & binary, std::vector<cv::Point2f> & quad,
    double img_area);

  // 在方框内部区域找圆心，同时输出圆的等效半径(像素，供尺寸比例校验)
  bool findCircleCenter(const cv::Mat & binary, const std::vector<cv::Point2f> & quad,
    cv::Point2f & center, double & radius);

  // 计算4角重投影误差(像素平均)，用于剔除坏解
  double reprojError(const std::vector<cv::Point3f> & obj_pts,
    const std::vector<cv::Point2f> & img_pts,
    const cv::Vec3d & rvec, const cv::Vec3d & tvec) const;

  // 把四边形 4 角排成 左上,右上,右下,左下
  static void orderCorners(std::vector<cv::Point2f> & quad);

  // 构造与 corners 对应的 3D 物体点(圆心为原点)
  std::vector<cv::Point3f> buildObjectPoints() const;
};

}  // namespace board_pose_detector

#endif  // BOARD_POSE_DETECTOR__BOARDDETECTOR_H_
