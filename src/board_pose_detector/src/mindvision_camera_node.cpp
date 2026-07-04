// 迈德威视(MindVision)工业相机取流节点
// ============================================================================
// 参考 SDK demo: linuxSDK/demo/OpenCv/main.cpp
// 流程: CameraSdkInit -> CameraEnumerateDevice -> CameraInit
//       -> CameraGetCapability -> CameraSetIspOutFormat(BGR8)
//       -> 手动锁定曝光/增益 -> CameraPlay
//       -> 循环 CameraGetImageBuffer + CameraImageProcess -> cv::Mat
//       -> cv_bridge 发布 /image_raw -> CameraReleaseImageBuffer
//
// 发布话题与海康节点一致(/image_raw, bgr8)，下游解算节点无需区分相机来源。
// 曝光/增益通过 ROS 参数(YAML)在启动时一次性写入相机，保证成像亮度稳定；
// 注意: 光圈与对焦是镜头上的机械环，SDK 无法设置，需手动拧好并锁死。
// ============================================================================

#include <cv_bridge/cv_bridge.h>

#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "CameraApi.h"

class MindVisionCameraNode : public rclcpp::Node
{
public:
  MindVisionCameraNode()
  : Node("mindvision_camera_node")
  {
    // ---- 参数声明 ----
    frame_id_ = declare_parameter<std::string>("frame_id", "camera_optical_frame");
    exposure_time_ = declare_parameter<double>("exposure_time", 5000.0);  // 微秒
    analog_gain_ = declare_parameter<int>("analog_gain", -1);  // <0 表示不设置，用相机默认

    image_pub_ = create_publisher<sensor_msgs::msg::Image>("/image_raw", 10);

    if (!initCamera()) {
      RCLCPP_FATAL(
        get_logger(),
        "MindVision 相机初始化失败 —— 节点保持存活但空闲。"
        "请检查: SDK 是否安装(/usr/lib/libMVSDK.so)? 相机是否连接? "
        "udev 规则是否已装(88-mvusb.rules)?");
      return;
    }

    // 15ms 定时抓帧 (~66fps 上限，实际受曝光时间限制)
    timer_ = create_wall_timer(
      std::chrono::milliseconds(15),
      std::bind(&MindVisionCameraNode::grabImage, this));
  }

  ~MindVisionCameraNode()
  {
    if (h_camera_ >= 0) {
      CameraUnInit(h_camera_);
    }
    if (rgb_buffer_) {
      free(rgb_buffer_);
    }
  }

private:
  int h_camera_ = -1;
  unsigned char * rgb_buffer_ = nullptr;
  int channel_ = 3;

  std::string frame_id_;
  double exposure_time_;
  int analog_gain_;

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
  rclcpp::TimerBase::SharedPtr timer_;

  bool initCamera()
  {
    CameraSdkInit(1);

    tSdkCameraDevInfo dev_list[4];
    int camera_counts = 4;
    int status = CameraEnumerateDevice(dev_list, &camera_counts);
    if (status != CAMERA_STATUS_SUCCESS || camera_counts == 0) {
      RCLCPP_ERROR(get_logger(), "未找到迈德威视相机 (status=%d)", status);
      return false;
    }
    RCLCPP_INFO(get_logger(), "找到 %d 台迈德威视相机，使用第 0 台", camera_counts);

    status = CameraInit(&dev_list[0], -1, -1, &h_camera_);
    if (status != CAMERA_STATUS_SUCCESS) {
      RCLCPP_ERROR(get_logger(), "CameraInit 失败 (status=%d)", status);
      h_camera_ = -1;
      return false;
    }

    tSdkCameraCapbility capability;
    CameraGetCapability(h_camera_, &capability);

    // 分配 RGB 输出缓冲区(按最大分辨率)
    rgb_buffer_ = static_cast<unsigned char *>(
      malloc(
        capability.sResolutionRange.iHeightMax *
        capability.sResolutionRange.iWidthMax * 3));

    // 设置输出格式: 彩色 sensor 输出 BGR8，黑白 sensor 输出 MONO8
    if (capability.sIspCapacity.bMonoSensor) {
      channel_ = 1;
      CameraSetIspOutFormat(h_camera_, CAMERA_MEDIA_TYPE_MONO8);
    } else {
      channel_ = 3;
      CameraSetIspOutFormat(h_camera_, CAMERA_MEDIA_TYPE_BGR8);
    }

    // ---- 手动锁定曝光(关自动曝光 + 设固定曝光时间) ----
    CameraSetAeState(h_camera_, FALSE);              // 关闭自动曝光
    CameraSetExposureTime(h_camera_, exposure_time_); // 单位: 微秒
    if (analog_gain_ >= 0) {
      CameraSetAnalogGain(h_camera_, analog_gain_);
    }
    RCLCPP_INFO(
      get_logger(), "曝光已锁定: %.1f us, 增益: %d", exposure_time_, analog_gain_);

    CameraPlay(h_camera_);
    RCLCPP_INFO(get_logger(), "迈德威视相机启动成功");
    return true;
  }

  void grabImage()
  {
    if (h_camera_ < 0) {
      return;
    }

    tSdkFrameHead frame_info;
    BYTE * raw_buffer = nullptr;

    int status = CameraGetImageBuffer(h_camera_, &frame_info, &raw_buffer, 1000);
    if (status != CAMERA_STATUS_SUCCESS) {
      RCLCPP_WARN(get_logger(), "CameraGetImageBuffer 失败: %d", status);
      return;
    }

    // ISP 处理: raw -> rgb_buffer_ (根据 sensor 类型输出 BGR8 或 MONO8)
    CameraImageProcess(h_camera_, raw_buffer, rgb_buffer_, &frame_info);

    cv::Mat image(
      cv::Size(frame_info.iWidth, frame_info.iHeight),
      channel_ == 1 ? CV_8UC1 : CV_8UC3,
      rgb_buffer_);

    cv::Mat bgr;
    if (channel_ == 1) {
      cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    } else {
      bgr = image;  // 已是 BGR8
    }

    auto msg = cv_bridge::CvImage(
      std_msgs::msg::Header(), "bgr8", bgr).toImageMsg();
    msg->header.stamp = this->now();
    msg->header.frame_id = frame_id_;

    image_pub_->publish(*msg);

    // 必须释放，否则下次 GetImageBuffer 会阻塞
    CameraReleaseImageBuffer(h_camera_, raw_buffer);
  }
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MindVisionCameraNode>());
  rclcpp::shutdown();
  return 0;
}
