#include <rclcpp/rclcpp.hpp>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include "armor_detector/ArmorDetector.h"
#include "armor_detector/msg/armor_result.hpp"
#include <geometry_msgs/msg/point.hpp>

class ArmorDetectorNode : public rclcpp::Node {
public:
    ArmorDetectorNode() : Node("armor_detector_node") {
        // 声明参数
        declare_parameter<std::string>("image_topic", "/image_raw");
        declare_parameter<bool>("debug", true);

        // 读取 DetectorParams 中的所有参数
        DetectorParams params;
        getParam(params.red_h_low1, "red_h_low1", 0);
        getParam(params.red_h_high1, "red_h_high1", 30);
        getParam(params.red_h_low2, "red_h_low2", 170);
        getParam(params.red_h_high2, "red_h_high2", 180);
        getParam(params.red_s_low, "red_s_low", 50);
        getParam(params.red_v_low, "red_v_low", 30);
        getParam(params.blue_h_low, "blue_h_low", 70);
        getParam(params.blue_h_high, "blue_h_high", 140);
        getParam(params.blue_s_low, "blue_s_low", 120);
        getParam(params.blue_v_low, "blue_v_low", 50);
        getParam(params.use_overexpose, "use_overexpose", true);
        getParam(params.overexpose_thresh, "overexpose_thresh", 220);
        getParam(params.gamma, "gamma", 1.0);
        getParam(params.light_area_min, "light_area_min", 5.0);
        getParam(params.light_area_max, "light_area_max", 1000.0);
        getParam(params.light_ratio_min, "light_ratio_min", 2.0);
        getParam(params.light_ratio_max, "light_ratio_max", 20.0);
        getParam(params.light_angle_min, "light_angle_min", 70.0);
        getParam(params.light_angle_max, "light_angle_max", 110.0);
        getParam(params.pair_ang_diff_max, "pair_ang_diff_max", 30.0);
        getParam(params.pair_h_diff_max, "pair_h_diff_max", 0.6);
        getParam(params.pair_dy_ratio_max, "pair_dy_ratio_max", 0.8);
        getParam(params.pair_dx_ratio_min, "pair_dx_ratio_min", 0.8);
        getParam(params.pair_dx_ratio_max, "pair_dx_ratio_max", 5.0);
        getParam(params.armor_ratio_min, "armor_ratio_min", 0.7);
        getParam(params.armor_ratio_max, "armor_ratio_max", 3.5);
        getParam(params.morph_color_close_size, "morph_color_close_size", 3);
        getParam(params.morph_color_open_size, "morph_color_open_size", 1);
        getParam(params.morph_final_close_size, "morph_final_close_size", 1);
        getParam(params.morph_final_open_size, "morph_final_open_size", 1);

        detector_.setParams(params);

        // 订阅图像
        std::string image_topic = get_parameter("image_topic").as_string();
        subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
            image_topic, rclcpp::SensorDataQoS(),
            std::bind(&ArmorDetectorNode::imageCallback, this, std::placeholders::_1));

        // 发布结果和调试图像
        result_pub_ = this->create_publisher<armor_detector::msg::ArmorResult>("/armor_result", 10);
        debug_image_pub_ = this->create_publisher<sensor_msgs::msg::Image>("/armor_debug_image", 10);
    }

private:
    template<typename T>
    void getParam(T& param, const std::string& name, const T& default_value) {
        param = this->declare_parameter(name, default_value);
    }

    void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg) {
        // 转换图像
        cv_bridge::CvImagePtr cv_ptr;
        try {
            cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
        } catch (cv_bridge::Exception& e) {
            RCLCPP_ERROR(this->get_logger(), "cv_bridge exception: %s", e.what());
            return;
        }

        // 检测
        auto results = detector_.detect(cv_ptr->image);

        // 发布结果
        for (const auto& res : results) {
            armor_detector::msg::ArmorResult msg;
            msg.color = res.color;
            msg.points.clear();
            for (int i = 0; i < 4; ++i) {
                geometry_msgs::msg::Point p;
                p.x = res.points[i].x;
                p.y = res.points[i].y;
                p.z = 0.0;
                msg.points.push_back(p);
            }
            result_pub_->publish(msg);
        }
        // 发布调试图像（在原图上画框）
        if (get_parameter("debug").as_bool()) {
            cv::Mat debug_img = cv_ptr->image.clone();
            for (const auto& res : results) {
                cv::Scalar color = (res.color == 0) ? cv::Scalar(0,0,255) : cv::Scalar(255,0,0);
                for (int i = 0; i < 4; ++i) {
                    cv::line(debug_img, res.points[i], res.points[(i+1)%4], color, 2);
                }
                cv::putText(debug_img, (res.color == 0 ? "RED" : "BLUE"), res.points[0],
                            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
            }
            auto debug_msg = cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", debug_img).toImageMsg();
            debug_image_pub_->publish(*debug_msg);
        }
    }

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
    rclcpp::Publisher<armor_detector::msg::ArmorResult>::SharedPtr result_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_image_pub_;
    ArmorDetector detector_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ArmorDetectorNode>());
    rclcpp::shutdown();
    return 0;
}