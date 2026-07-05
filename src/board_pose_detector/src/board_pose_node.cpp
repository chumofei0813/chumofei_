// board_pose_node: 目标板位姿解算 ROS2 节点
// ============================================================================
// 订阅:  /image_raw            (sensor_msgs/Image, bgr8) —— 海康或迈德威视发布
// 发布:  /board_pose           (geometry_msgs/PoseStamped) —— 目标相对相机位姿
//        /board_pose/debug_image (sensor_msgs/Image) —— 检测框+关键点+坐标轴
//        TF: camera_optical_frame -> target_board
// 参数:  camera_info_path (内参YAML路径)、目标板尺寸、检测阈值等
// ============================================================================
#include <cv_bridge/cv_bridge.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_ros/transform_broadcaster.h>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <yaml-cpp/yaml.h>

#include "board_pose_detector/BoardDetector.h"

using std::placeholders::_1;

class BoardPoseNode : public rclcpp::Node
{
public:
  BoardPoseNode()
  : Node("board_pose_node")
  {
    // ---- 目标板尺寸参数(mm) ----
    board_pose_detector::BoardParams p;
    p.outer_size = declare_parameter<double>("outer_size", 150.0);
    p.inner_size = declare_parameter<double>("inner_size", 115.0);
    p.circle_diameter = declare_parameter<double>("circle_diameter", 50.0);
    p.binary_thresh = declare_parameter<int>("binary_thresh", 80);
    p.use_otsu = declare_parameter<bool>("use_otsu", true);
    p.min_quad_squareness = declare_parameter<double>("min_quad_squareness", 0.6);
    p.circle_ratio_min = declare_parameter<double>("circle_ratio_min", 0.22);
    p.circle_ratio_max = declare_parameter<double>("circle_ratio_max", 0.48);
    p.max_reproj_error = declare_parameter<double>("max_reproj_error", 6.0);
    detector_.setParams(p);

    // ---- 帧 & 话题 ----
    camera_frame_ = declare_parameter<std::string>("camera_frame", "camera_optical_frame");
    board_frame_ = declare_parameter<std::string>("board_frame", "target_board");
    publish_debug_ = declare_parameter<bool>("publish_debug", true);

    // ---- 加载相机内参 ----
    std::string camera_info_path = declare_parameter<std::string>("camera_info_path", "");
    if (!loadCameraInfo(camera_info_path)) {
      RCLCPP_WARN(
        get_logger(),
        "未加载相机内参(camera_info_path='%s')，将只输出2D检测，无位姿。",
        camera_info_path.c_str());
    }

    // ---- 发布/订阅 ----
    pose_pub_ = create_publisher<geometry_msgs::msg::PoseStamped>("/board_pose", 10);
    if (publish_debug_) {
      debug_pub_ = create_publisher<sensor_msgs::msg::Image>(
        "/board_pose/debug_image", 10);
    }
    tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

    image_sub_ = create_subscription<sensor_msgs::msg::Image>(
      "/image_raw", rclcpp::SensorDataQoS(),
      std::bind(&BoardPoseNode::imageCallback, this, _1));

    RCLCPP_INFO(get_logger(), "board_pose_node 启动，等待 /image_raw ...");
  }

private:
  board_pose_detector::BoardDetector detector_;
  std::string camera_frame_, board_frame_;
  bool publish_debug_ = true;

  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_pub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  bool loadCameraInfo(const std::string & path)
  {
    if (path.empty()) {return false;}
    // 用 yaml-cpp 解析标准 camera_info YAML
    // (OpenCV FileStorage 不兼容 ROS 风格 YAML，见 armor_detector W6 记录)
    YAML::Node root;
    try {
      root = YAML::LoadFile(path);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "无法解析内参文件 %s: %s", path.c_str(), e.what());
      return false;
    }
    if (!root["camera_matrix"] || !root["distortion_coefficients"]) {
      RCLCPP_ERROR(get_logger(), "内参文件缺少 camera_matrix 或 distortion_coefficients");
      return false;
    }
    std::vector<double> cm_data =
      root["camera_matrix"]["data"].as<std::vector<double>>();
    std::vector<double> dc_data =
      root["distortion_coefficients"]["data"].as<std::vector<double>>();
    if (cm_data.size() != 9) {
      RCLCPP_ERROR(get_logger(), "camera_matrix data 长度应为9，实际 %zu", cm_data.size());
      return false;
    }
    cv::Mat K = cv::Mat(3, 3, CV_64F, cm_data.data()).clone();
    cv::Mat D = cv::Mat(1, static_cast<int>(dc_data.size()), CV_64F, dc_data.data()).clone();
    detector_.setCameraInfo(K, D);
    RCLCPP_INFO(
      get_logger(), "已加载内参: fx=%.1f fy=%.1f cx=%.1f cy=%.1f",
      K.at<double>(0, 0), K.at<double>(1, 1),
      K.at<double>(0, 2), K.at<double>(1, 2));
    return true;
  }

  void imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
  {
    cv::Mat bgr;
    try {
      bgr = cv_bridge::toCvShare(msg, "bgr8")->image;
    } catch (const cv_bridge::Exception & e) {
      RCLCPP_ERROR(get_logger(), "cv_bridge 转换失败: %s", e.what());
      return;
    }

    cv::Mat debug;
    auto result = detector_.detect(bgr, publish_debug_ ? &debug : nullptr);

    // 发布位姿(仅当解出 tvec)
    if (result.found && detector_.hasCameraInfo() && result.distance > 1e-6) {
      publishPose(result, msg->header.stamp);
    }

    // 发布调试图
    if (publish_debug_ && !debug.empty()) {
      auto dbg_msg = cv_bridge::CvImage(msg->header, "bgr8", debug).toImageMsg();
      debug_pub_->publish(*dbg_msg);
    }
  }

  void publishPose(
    const board_pose_detector::BoardResult & r, const rclcpp::Time & stamp)
  {
    // rvec -> 四元数
    cv::Mat R;
    cv::Rodrigues(r.rvec, R);
    tf2::Matrix3x3 tf_R(
      R.at<double>(0, 0), R.at<double>(0, 1), R.at<double>(0, 2),
      R.at<double>(1, 0), R.at<double>(1, 1), R.at<double>(1, 2),
      R.at<double>(2, 0), R.at<double>(2, 1), R.at<double>(2, 2));
    tf2::Quaternion q;
    tf_R.getRotation(q);

    // 位置: mm -> m (ROS 惯例用米)
    double x = r.tvec[0] / 1000.0;
    double y = r.tvec[1] / 1000.0;
    double z = r.tvec[2] / 1000.0;

    geometry_msgs::msg::PoseStamped pose;
    pose.header.stamp = stamp;
    pose.header.frame_id = camera_frame_;
    pose.pose.position.x = x;
    pose.pose.position.y = y;
    pose.pose.position.z = z;
    pose.pose.orientation.x = q.x();
    pose.pose.orientation.y = q.y();
    pose.pose.orientation.z = q.z();
    pose.pose.orientation.w = q.w();
    pose_pub_->publish(pose);

    // 广播 TF
    geometry_msgs::msg::TransformStamped tf_msg;
    tf_msg.header.stamp = stamp;
    tf_msg.header.frame_id = camera_frame_;
    tf_msg.child_frame_id = board_frame_;
    tf_msg.transform.translation.x = x;
    tf_msg.transform.translation.y = y;
    tf_msg.transform.translation.z = z;
    tf_msg.transform.rotation.x = q.x();
    tf_msg.transform.rotation.y = q.y();
    tf_msg.transform.rotation.z = q.z();
    tf_msg.transform.rotation.w = q.w();
    tf_broadcaster_->sendTransform(tf_msg);

    RCLCPP_INFO_THROTTLE(
      get_logger(), *get_clock(), 500,
      "位姿: dist=%.0fmm  X=%.0f Y=%.0f Z=%.0f (mm) | roll=%.1f pitch=%.1f yaw=%.1f",
      r.distance, r.tvec[0], r.tvec[1], r.tvec[2], r.roll, r.pitch, r.yaw);
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<BoardPoseNode>());
  rclcpp::shutdown();
  return 0;
}
