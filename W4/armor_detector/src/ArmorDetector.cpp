#include "armor_detector/ArmorDetector.h"
#include <algorithm>

using namespace cv;
using namespace std;

ArmorDetector::ArmorDetector() {}

void ArmorDetector::setParams(const DetectorParams& params) {
    params_ = params;
}

Mat ArmorDetector::split_overexpose(const Mat& hsv) const {
    vector<Mat> channels;
    split(hsv, channels);
    Mat v_channel = channels[2];
    Mat v_mask;
    threshold(v_channel, v_mask, params_.overexpose_thresh, 255, THRESH_BINARY);
    Mat kernel_close = getStructuringElement(MORPH_RECT, Size(5,5));
    morphologyEx(v_mask, v_mask, MORPH_CLOSE, kernel_close);
    Mat kernel_open = getStructuringElement(MORPH_RECT, Size(2,2));
    morphologyEx(v_mask, v_mask, MORPH_OPEN, kernel_open);
    return v_mask;
}

void ArmorDetector::getMasks(const Mat& bgr, Mat& red_mask, Mat& blue_mask) const {
    // Gamma矫正
    Mat gamma_corrected;
    bgr.convertTo(gamma_corrected, CV_32F, 1.0/255.0);
    pow(gamma_corrected, params_.gamma, gamma_corrected);
    gamma_corrected.convertTo(gamma_corrected, CV_8U, 255.0);

    Mat hsv;
    cvtColor(gamma_corrected, hsv, COLOR_BGR2HSV);

    // 红色mask
    Mat red1, red2;
    inRange(hsv, Scalar(params_.red_h_low1, params_.red_s_low, params_.red_v_low),
            Scalar(params_.red_h_high1, 255, 255), red1);
    inRange(hsv, Scalar(params_.red_h_low2, params_.red_s_low, params_.red_v_low),
            Scalar(params_.red_h_high2, 255, 255), red2);
    Mat red_color = red1 | red2;

    // 蓝色mask
    Mat blue_color;
    inRange(hsv, Scalar(params_.blue_h_low, params_.blue_s_low, params_.blue_v_low),
            Scalar(params_.blue_h_high, 255, 255), blue_color);

    // 颜色mask形态学：先闭后开
    Mat kernel_color_close = getStructuringElement(MORPH_RECT, Size(params_.morph_color_close_size, params_.morph_color_close_size));
    morphologyEx(red_color, red_color, MORPH_CLOSE, kernel_color_close);
    morphologyEx(blue_color, blue_color, MORPH_CLOSE, kernel_color_close);

    Mat kernel_color_open = getStructuringElement(MORPH_RECT, Size(params_.morph_color_open_size, params_.morph_color_open_size));
    morphologyEx(red_color, red_color, MORPH_OPEN, kernel_color_open);
    morphologyEx(blue_color, blue_color, MORPH_OPEN, kernel_color_open);

    // 过曝提取（可选）
    if (params_.use_overexpose) {
        Mat v_mask = split_overexpose(hsv);
        red_mask = red_color & v_mask;
        blue_mask = blue_color & v_mask;
    } else {
        red_mask = red_color;
        blue_mask = blue_color;
    }

    // 最终mask形态学
    Mat kernel_final_close = getStructuringElement(MORPH_RECT, Size(params_.morph_final_close_size, params_.morph_final_close_size));
    morphologyEx(red_mask, red_mask, MORPH_CLOSE, kernel_final_close);
    morphologyEx(blue_mask, blue_mask, MORPH_CLOSE, kernel_final_close);

    Mat kernel_final_open = getStructuringElement(MORPH_RECT, Size(params_.morph_final_open_size, params_.morph_final_open_size));
    morphologyEx(red_mask, red_mask, MORPH_OPEN, kernel_final_open);
    morphologyEx(blue_mask, blue_mask, MORPH_OPEN, kernel_final_open);
}

void ArmorDetector::getEndpoints(const RotatedRect& rect, Point2f& top, Point2f& bottom) const {
    Point2f pts[4];
    rect.points(pts);
    sort(pts, pts+4, [](const Point2f& a, const Point2f& b) { return a.y < b.y; });
    top = (pts[0] + pts[1]) / 2.0f;
    bottom = (pts[2] + pts[3]) / 2.0f;
}

vector<ArmorDetector::LightBar> ArmorDetector::extractLights(const Mat& mask, int color_flag) const {
    vector<vector<Point>> contours;
    findContours(mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    vector<LightBar> lights;
    for (auto& cnt : contours) {
        
        double area = contourArea(cnt);
        if (area < params_.light_area_min || area > params_.light_area_max) continue;

        RotatedRect r = minAreaRect(cnt);
        float w = r.size.width, h = r.size.height;
        if (h < w) swap(h, w);
        
        float ratio = h / w;
        if (ratio < params_.light_ratio_min || ratio > params_.light_ratio_max) continue;

        float ang = fabs(r.angle);
        if (!(ang > params_.light_angle_min && ang < params_.light_angle_max)) continue;

        LightBar lb;
        lb.rect = r;
        lb.center = r.center;
        lb.angle = ang;
        lb.height = h;
        lb.width = w;
        lb.color = color_flag;
        lights.push_back(lb);
    }
    return lights;
}

vector<ArmorResult> ArmorDetector::pairLights(const vector<LightBar>& lights, DebugInfo* debug) const {
    vector<ArmorResult> armors;
    for (size_t i = 0; i < lights.size(); ++i) {
        for (size_t j = i+1; j < lights.size(); ++j) {
            const LightBar& l1 = lights[i];
            const LightBar& l2 = lights[j];
            if (l1.color != l2.color) continue;

            float ang_diff = fabs(l1.angle - l2.angle);
            float h_diff = fabs(l1.height - l2.height) / max(l1.height, l2.height);
            float avg_len = (l1.height + l2.height) / 2.0f;
            float dx = fabs(l1.center.x - l2.center.x);
            float dy = fabs(l1.center.y - l2.center.y);
            float dx_ratio = dx / avg_len;
            float dy_ratio = dy / avg_len;

            if (ang_diff > params_.pair_ang_diff_max) continue;
            if (h_diff > params_.pair_h_diff_max) continue;
            if (dy_ratio > params_.pair_dy_ratio_max) continue;
            if (dx_ratio < params_.pair_dx_ratio_min || dx_ratio > params_.pair_dx_ratio_max) continue;

            const LightBar* left = (l1.center.x < l2.center.x) ? &l1 : &l2;
            const LightBar* right = (l1.center.x < l2.center.x) ? &l2 : &l1;

            Point2f left_top, left_bottom, right_top, right_bottom;
            getEndpoints(left->rect, left_top, left_bottom);
            getEndpoints(right->rect, right_top, right_bottom);

            float armor_width = right_top.x - left_top.x;
            float armor_height = left_bottom.y - left_top.y;
            if (armor_height > 0 && (armor_width / armor_height < params_.armor_ratio_min || armor_width / armor_height > params_.armor_ratio_max)) continue;

            ArmorResult res;
            res.points[0] = left_top;
            res.points[1] = right_top;
            res.points[2] = right_bottom;
            res.points[3] = left_bottom;
            res.color = l1.color;
            armors.push_back(res);

            if (debug) {
                debug->paired_lights.push_back({left->rect, right->rect});
            }
        }
    }
    return armors;
}

vector<ArmorResult> ArmorDetector::detect(const Mat& bgr_frame, DebugInfo* debug) {
    if (bgr_frame.empty()) {
        return vector<ArmorResult>();
    }

    Mat red_mask, blue_mask;
    getMasks(bgr_frame, red_mask, blue_mask);

    if (debug) {
        debug->red_mask = red_mask.clone();
        debug->blue_mask = blue_mask.clone();
    }

    vector<LightBar> red_lights = extractLights(red_mask, 0);
    vector<LightBar> blue_lights = extractLights(blue_mask, 1);
    vector<LightBar> all_lights;
    all_lights.insert(all_lights.end(), red_lights.begin(), red_lights.end());
    all_lights.insert(all_lights.end(), blue_lights.begin(), blue_lights.end());

    if (debug) {
        for (auto& l : red_lights) debug->red_light_candidates.push_back(l.rect);
        for (auto& l : blue_lights) debug->blue_light_candidates.push_back(l.rect);
    }

    return pairLights(all_lights, debug);
}