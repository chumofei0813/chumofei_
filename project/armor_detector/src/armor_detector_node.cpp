#include <rclcpp/rclcpp.hpp>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include <geometry_msgs/msg/point.hpp>

#include "armor_detector/ArmorDetector.h"
#include "armor_detector/msg/armor_result.hpp"

// ============================================================================
// ArmorDetectorNode — 装甲板检测 ROS2 节点
// ============================================================================
// 职责：
//   1. 订阅图像话题
//   2. 从参数服务器读取检测参数，配置 ArmorDetector
//   3. 调用检测逻辑（ArmorDetector::detect），获取装甲板结果和调试信息
//   4. 发布检测结果和可选的调试图像
//
// 检测逻辑（ArmorDetector）不依赖 ROS，仅接收 cv::Mat，返回 ArmorResult。
// 同一套检测逻辑可被离线图片、视频、bag 和真实相机复用。
// ============================================================================

class ArmorDetectorNode : public rclcpp::Node {
public:
    ArmorDetectorNode() : Node("armor_detector_node") {
        // 基础参数
        declare_parameter<std::string>("image_topic", "/image_raw");
        declare_parameter<bool>("debug", true);
        declare_parameter<int>("debug_level", 2);     // 0=off, 1=装甲板框, 2=+灯条, 3=+mask
        declare_parameter<bool>("publish_endpoints", false);

        // ========== 检测参数 ==========
        DetectorParams params;

        // --- Gamma 矫正 ---
        getParam(params.gamma, "gamma", 1.0);

        // --- HSV 阈值：红色（两个 H 区间取并集）---
        getParam(params.red_h_low1,   "red_h_low1",   0);
        getParam(params.red_h_high1,  "red_h_high1",  30);
        getParam(params.red_h_low2,   "red_h_low2",   170);
        getParam(params.red_h_high2,  "red_h_high2",  180);
        getParam(params.red_s_low,    "red_s_low",    50);
        getParam(params.red_v_low,    "red_v_low",    30);

        // --- HSV 阈值：蓝色 ---
        getParam(params.blue_h_low,   "blue_h_low",   70);
        getParam(params.blue_h_high,  "blue_h_high",  140);
        getParam(params.blue_s_low,   "blue_s_low",   120);
        getParam(params.blue_v_low,   "blue_v_low",   50);

        // --- 颜色 mask 形态学（先闭后开）---
        getParam(params.morph_color_close_size, "morph_color_close_size", 3);
        getParam(params.morph_color_open_size,  "morph_color_open_size",  1);

        // --- 过曝提取（V 通道辅助）---
        getParam(params.use_overexpose,    "use_overexpose",    false);
        getParam(params.overexpose_thresh, "overexpose_thresh", 220);

        // --- 最终 mask 形态学 ---
        getParam(params.morph_final_close_size, "morph_final_close_size", 1);
        getParam(params.morph_final_open_size,  "morph_final_open_size",  1);

        // --- 灯条筛选 ---
        getParam(params.light_area_min,  "light_area_min",  5.0);
        getParam(params.light_area_max,  "light_area_max",  50000.0);
        getParam(params.light_ratio_min, "light_ratio_min", 2.0);
        getParam(params.light_ratio_max, "light_ratio_max", 20.0);
        getParam(params.light_angle_min, "light_angle_min", 70.0);
        getParam(params.light_angle_max, "light_angle_max", 110.0);

        // --- 灯条配对 ---
        getParam(params.pair_ang_diff_max, "pair_ang_diff_max", 30.0);
        getParam(params.pair_h_diff_max,   "pair_h_diff_max",   0.6);
        getParam(params.pair_dy_ratio_max, "pair_dy_ratio_max", 0.8);
        getParam(params.pair_dx_ratio_min, "pair_dx_ratio_min", 0.8);
        getParam(params.pair_dx_ratio_max, "pair_dx_ratio_max", 5.0);

        // --- 装甲板宽高比 ---
        getParam(params.armor_ratio_min, "armor_ratio_min", 0.7);
        getParam(params.armor_ratio_max, "armor_ratio_max", 3.5);

        detector_.setParams(params);

        // --- 订阅图像 ---
        std::string image_topic = get_parameter("image_topic").as_string();
        subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
            image_topic, rclcpp::SensorDataQoS(),
            std::bind(&ArmorDetectorNode::imageCallback, this, std::placeholders::_1));

        // --- 发布者 ---
        result_pub_ = this->create_publisher<armor_detector::msg::ArmorResult>(
            "/armor_result", 10);
        debug_image_pub_ = this->create_publisher<sensor_msgs::msg::Image>(
            "/armor_debug_image", 10);
        endpoints_pub_ = this->create_publisher<geometry_msgs::msg::Point>(
            "/armor_endpoints", 10);

        RCLCPP_INFO(this->get_logger(),
            "ArmorDetectorNode started. image_topic=%s, debug=%d, debug_level=%ld",
            image_topic.c_str(),
            get_parameter("debug").as_bool(),
            get_parameter("debug_level").as_int());
    }

private:
    // ---- 模板：声明参数并从 YAML / 默认值读取 ----
    template<typename T>
    void getParam(T& param, const std::string& name, const T& default_value) {
        param = this->declare_parameter(name, default_value);
    }

    // ---- 图像回调 ----
    void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg) {
        // ROS 图像 → cv::Mat
        cv_bridge::CvImagePtr cv_ptr;
        try {
            cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
        } catch (cv_bridge::Exception& e) {
            RCLCPP_ERROR(this->get_logger(), "cv_bridge exception: %s", e.what());
            return;
        }

        // ---- 调用检测逻辑 ----
        DebugInfo debug_info;
        auto results = detector_.detect(cv_ptr->image, &debug_info);

        // ---- 发布装甲板结果 ----
        for (const auto& res : results) {
            armor_detector::msg::ArmorResult arm_msg;
            arm_msg.color = res.color;
            for (int i = 0; i < 4; ++i) {
                geometry_msgs::msg::Point p;
                p.x = res.points[i].x;
                p.y = res.points[i].y;
                p.z = 0.0;
                arm_msg.points.push_back(p);
            }
            result_pub_->publish(arm_msg);
        }

        // ---- 发布灯条端点（可选）----
        if (get_parameter("publish_endpoints").as_bool()) {
            publishEndpoints(debug_info);
        }

        // ---- 发布调试图像 ----
        if (get_parameter("debug").as_bool()) {
            publishDebugImage(cv_ptr->image, results, debug_info);
        }
    }

    // ---- 发布灯条端点 ----
    void publishEndpoints(const DebugInfo& debug) {
        auto publish_rect_endpoints = [this](const std::vector<cv::RotatedRect>& rects, int /* color */) {
            for (const auto& r : rects) {
                cv::Point2f pts[4];
                r.points(pts);
                // 按 y 排序，取上下两个中点
                std::sort(pts, pts+4, [](const cv::Point2f& a, const cv::Point2f& b) { return a.y < b.y; });
                cv::Point2f top = (pts[0] + pts[1]) * 0.5f;
                cv::Point2f bottom = (pts[2] + pts[3]) * 0.5f;

                auto make_point = [](float x, float y) {
                    geometry_msgs::msg::Point p;
                    p.x = x; p.y = y; p.z = 0.0;
                    return p;
                };
                endpoints_pub_->publish(make_point(top.x, top.y));
                endpoints_pub_->publish(make_point(bottom.x, bottom.y));
            }
        };
        publish_rect_endpoints(debug.red_light_candidates, 0);
        publish_rect_endpoints(debug.blue_light_candidates, 1);
    }

    // ---- 发布调试图像 ----
    void publishDebugImage(const cv::Mat& frame,
                           const std::vector<ArmorResult>& results,
                           const DebugInfo& debug) {
        int level = get_parameter("debug_level").as_int();
        cv::Mat debug_img = frame.clone();

        // Level 3: 叠加颜色 mask
        if (level >= 3) {
            if (!debug.red_mask.empty()) {
                cv::Mat red_overlay;
                cv::cvtColor(debug.red_mask, red_overlay, cv::COLOR_GRAY2BGR);
                red_overlay.setTo(cv::Scalar(0, 0, 255), debug.red_mask);
                cv::addWeighted(debug_img, 1.0, red_overlay, 0.3, 0, debug_img);
            }
            if (!debug.blue_mask.empty()) {
                cv::Mat blue_overlay;
                cv::cvtColor(debug.blue_mask, blue_overlay, cv::COLOR_GRAY2BGR);
                blue_overlay.setTo(cv::Scalar(255, 0, 0), debug.blue_mask);
                cv::addWeighted(debug_img, 1.0, blue_overlay, 0.3, 0, debug_img);
            }
        }

        // Level 2+: 绘制灯条候选框
        if (level >= 2) {
            auto draw_rotated_rect = [&](const cv::RotatedRect& r, const cv::Scalar& color) {
                cv::Point2f pts[4];
                r.points(pts);
                for (int i = 0; i < 4; ++i)
                    cv::line(debug_img, pts[i], pts[(i+1)%4], color, 1);
            };
            for (const auto& r : debug.red_light_candidates)
                draw_rotated_rect(r, cv::Scalar(0, 100, 255));
            for (const auto& r : debug.blue_light_candidates)
                draw_rotated_rect(r, cv::Scalar(255, 100, 0));
        }

        // Level 1+: 绘制装甲板框和标签
        if (level >= 1) {
            for (const auto& res : results) {
                cv::Scalar color = (res.color == 0) ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 0, 0);
                for (int i = 0; i < 4; ++i)
                    cv::line(debug_img, res.points[i], res.points[(i+1)%4], color, 2);
                cv::putText(debug_img, (res.color == 0 ? "RED" : "BLUE"),
                            res.points[0], cv::FONT_HERSHEY_SIMPLEX, 0.5,
                            cv::Scalar(255, 255, 255), 1);
            }
        }

        // 配对连线（level >= 2）
        if (level >= 2) {
            for (const auto& pair : debug.paired_lights) {
                cv::line(debug_img, pair.first.center, pair.second.center,
                         cv::Scalar(0, 255, 255), 1);
            }
        }

        auto debug_msg = cv_bridge::CvImage(
            std_msgs::msg::Header(), "bgr8", debug_img).toImageMsg();
        debug_image_pub_->publish(*debug_msg);
    }

    // ---- 成员变量 ----
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
    rclcpp::Publisher<armor_detector::msg::ArmorResult>::SharedPtr result_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_image_pub_;
    rclcpp::Publisher<geometry_msgs::msg::Point>::SharedPtr endpoints_pub_;
    ArmorDetector detector_;
};

// ============================================================================
// main — 入口，仅负责初始化 ROS 和启动节点
// ============================================================================
int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ArmorDetectorNode>());
    rclcpp::shutdown();
    return 0;
}
