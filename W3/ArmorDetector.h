#ifndef ARMOR_DETECTOR_H
#define ARMOR_DETECTOR_H

#include <opencv2/opencv.hpp>
#include <vector>

// 装甲板结果
struct ArmorResult {
    std::vector<cv::Point2f> points; // 四个有序点：左上、右上、右下、左下
    int color;                       // 0:红色, 1:蓝色
};

// 调试信息
struct DebugInfo {
    cv::Mat red_mask;
    cv::Mat blue_mask;
    std::vector<cv::RotatedRect> red_light_candidates;
    std::vector<cv::RotatedRect> blue_light_candidates;
    std::vector<std::pair<cv::RotatedRect, cv::RotatedRect>> paired_lights; // 配对的灯条
};

class ArmorDetector {

public:
    
    ArmorDetector();
    
    std::vector<ArmorResult> detect(const cv::Mat& bgr_frame, DebugInfo* debug = nullptr);
    
private:

    struct LightBar {
        cv::RotatedRect rect;
        cv::Point2f center;
        float angle;
        float height;
        float width;
        int color;
    };
    
    // 过曝提取 V 通道 mask
    cv::Mat split_overexpose(const cv::Mat& hsv) const;
    
    // 生成mask
    void getMasks(const cv::Mat& bgr, cv::Mat& red_mask, cv::Mat& blue_mask) const;
    
    // 获取旋转矩形上下端点
    void getEndpoints(const cv::RotatedRect& rect, cv::Point2f& top, cv::Point2f& bottom) const;
    
    // 从 mask 中提取灯条候选
    std::vector<LightBar> extractLights(const cv::Mat& mask, int color_flag) const;
    
    // 灯条配对生成装甲板
    std::vector<ArmorResult> pairLights(const std::vector<LightBar>& lights) const;
};

#endif