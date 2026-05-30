#ifndef ARMOR_DETECTOR_H
#define ARMOR_DETECTOR_H

#include <opencv2/opencv.hpp>
#include <vector>

struct DetectorParams {
    // Gamma矫正
    double gamma = 1.0;
    
    // HSV阈值
    int red_h_low1 = 0, red_h_high1 = 30;
    int red_h_low2 = 170, red_h_high2 = 180;
    int red_s_low = 50, red_v_low = 50;
    int blue_h_low = 70, blue_h_high = 140;
    int blue_s_low = 120, blue_v_low = 50;

    // 颜色mask形态学（先闭后开）默认值：闭3，开1
    int morph_color_close_size = 3;
    int morph_color_open_size = 1;
    
    // 是否使用V通道过曝提取
    bool use_overexpose = true;
    int overexpose_thresh = 220;

    // 最终mask形态学（先闭后开）默认值：均为1（无效果）
    int morph_final_close_size = 1;
    int morph_final_open_size = 1;
    
    double light_area_min = 5.0, light_area_max = 1000.0;
    double light_ratio_min = 2.0, light_ratio_max = 20.0;
    double light_angle_min = 70.0, light_angle_max = 110.0; // 竖直方向范围

    double pair_ang_diff_max = 30.0;
    double pair_h_diff_max = 0.6;
    double pair_dy_ratio_max = 0.8;
    double pair_dx_ratio_min = 0.8, pair_dx_ratio_max = 5.0;

    double armor_ratio_min = 0.7, armor_ratio_max = 3.5;
};

struct ArmorResult {
    cv::Point2f points[4];  // 顺序：左上,右上,右下,左下
    int color;              // 0红,1蓝
};

struct DebugInfo {
    cv::Mat red_mask;
    cv::Mat blue_mask;
    std::vector<cv::RotatedRect> red_light_candidates;
    std::vector<cv::RotatedRect> blue_light_candidates;
    std::vector<std::pair<cv::RotatedRect, cv::RotatedRect>> paired_lights;
};

class ArmorDetector {
public:
    ArmorDetector();
    void setParams(const DetectorParams& params);
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

    DetectorParams params_;

    cv::Mat split_overexpose(const cv::Mat& hsv) const;
    void getMasks(const cv::Mat& bgr, cv::Mat& red_mask, cv::Mat& blue_mask) const;
    void getEndpoints(const cv::RotatedRect& rect, cv::Point2f& top, cv::Point2f& bottom) const;
    std::vector<LightBar> extractLights(const cv::Mat& mask, int color_flag) const;
    std::vector<ArmorResult> pairLights(const std::vector<LightBar>& lights, DebugInfo* debug) const;
};

#endif