#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.h>

#include <opencv2/opencv.hpp>

#include "MvCameraControl.h"

class HikCameraNode : public rclcpp::Node
{
public:
HikCameraNode()
: Node("hik_camera_node")
{
image_pub_ =
create_publisher<sensor_msgs::msg::Image>(
"/image_raw", 10);

    initCamera();

    timer_ =
        create_wall_timer(
            std::chrono::milliseconds(10),
            std::bind(
                &HikCameraNode::grabImage,
                this));
}

~HikCameraNode()
{
    if (handle_)
    {
        MV_CC_StopGrabbing(handle_);
        MV_CC_CloseDevice(handle_);
        MV_CC_DestroyHandle(handle_);
    }

    MV_CC_Finalize();
}

private:
void* handle_ = nullptr;

rclcpp::Publisher<
    sensor_msgs::msg::Image>::SharedPtr image_pub_;

rclcpp::TimerBase::SharedPtr timer_;

bool initCamera()
{
    int nRet = MV_CC_Initialize();

    if (nRet != MV_OK)
    {
        RCLCPP_ERROR(
            get_logger(),
            "MV_CC_Initialize failed");

        return false;
    }

    MV_CC_DEVICE_INFO_LIST stDeviceList;

    memset(
        &stDeviceList,
        0,
        sizeof(stDeviceList));

    nRet = MV_CC_EnumDevices(
        MV_USB_DEVICE,
        &stDeviceList);

    if (nRet != MV_OK ||
        stDeviceList.nDeviceNum == 0)
    {
        RCLCPP_ERROR(
            get_logger(),
            "No Hik camera found");

        return false;
    }

    nRet = MV_CC_CreateHandle(
        &handle_,
        stDeviceList.pDeviceInfo[0]);

    if (nRet != MV_OK)
    {
        RCLCPP_ERROR(
            get_logger(),
            "CreateHandle failed");

        return false;
    }

    nRet = MV_CC_OpenDevice(handle_);

    if (nRet != MV_OK)
    {
        RCLCPP_ERROR(
            get_logger(),
            "OpenDevice failed");

        return false;
    }

    nRet = MV_CC_StartGrabbing(handle_);

    if (nRet != MV_OK)
    {
        RCLCPP_ERROR(
            get_logger(),
            "StartGrabbing failed");

        return false;
    }

    RCLCPP_INFO(
        get_logger(),
        "Camera started successfully");

    return true;
}

void grabImage()
{
    MV_FRAME_OUT frame;

    memset(
        &frame,
        0,
        sizeof(MV_FRAME_OUT));

    int nRet =
        MV_CC_GetImageBuffer(
            handle_,
            &frame,
            1000);

    if (nRet != MV_OK)
    {
        RCLCPP_WARN(
            this->get_logger(),
            "GetImageBuffer failed: 0x%x",
            nRet);
        
            return;
    }

    cv::Mat raw(
        frame.stFrameInfo.nHeight,
        frame.stFrameInfo.nWidth,
        CV_8UC1,
        frame.pBufAddr);

    cv::Mat bgr;

    cv::cvtColor(
        raw,
        bgr,
        cv::COLOR_BayerBG2BGR);

    auto msg =
        cv_bridge::CvImage(
            std_msgs::msg::Header(),
            "bgr8",
            bgr)
            .toImageMsg();

    msg->header.stamp =
        this->now();

    image_pub_->publish(*msg);

    MV_CC_FreeImageBuffer(
        handle_,
        &frame);
}

};

int main(int argc, char** argv)
{
rclcpp::init(argc, argv);

rclcpp::spin(
    std::make_shared<HikCameraNode>());

rclcpp::shutdown();

return 0;

}