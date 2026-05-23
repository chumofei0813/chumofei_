#include "ArmorDetector.h"
#include <algorithm>

using namespace cv;
using namespace std;

ArmorDetector::ArmorDetector() {}

Mat ArmorDetector::split_overexpose(const Mat& hsv) const {
    vector<Mat> channels;
    split(hsv, channels);
    Mat v_channel = channels[2];
    Mat v_mask;
    threshold(v_channel, v_mask, 220, 255, THRESH_BINARY);
    Mat kernel_close = getStructuringElement(MORPH_RECT, Size(5,5));
    morphologyEx(v_mask, v_mask, MORPH_CLOSE, kernel_close);
    Mat kernel_open = getStructuringElement(MORPH_RECT, Size(2,2));
    morphologyEx(v_mask, v_mask, MORPH_OPEN, kernel_open);
    return v_mask;
}

void ArmorDetector::getMasks(const Mat& bgr, Mat& red_mask, Mat& blue_mask) const {
    // Gamma 矫正 （2.0）
    Mat gamma;
    bgr.convertTo(gamma, CV_32F, 1.0/255.0);
    pow(gamma, 2.0, gamma);
    gamma.convertTo(gamma, CV_8U, 255.0);
    
    Mat hsv;
    cvtColor(gamma, hsv, COLOR_BGR2HSV);
    
    // 红色 mask
    Mat red1, red2;
    inRange(hsv, Scalar(0, 50, 50), Scalar(30, 255, 255), red1);
    inRange(hsv, Scalar(170, 50, 50), Scalar(180, 255, 255), red2);
    Mat red_color = red1 | red2;
    
    // 蓝色 mask
    Mat blue_color;
    inRange(hsv, Scalar(70, 120, 50), Scalar(140, 255, 255), blue_color);
    
    Mat v_mask = split_overexpose(hsv);
    
    red_mask = red_color & v_mask;
    blue_mask = blue_color & v_mask;
    
    //形态学处理
    //cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
    //cv::Mat kernel_open = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
    //cv::morphologyEx(red_mask, red_mask, cv::MORPH_CLOSE, kernel_close);
    //cv::morphologyEx(red_mask, red_mask, cv::MORPH_OPEN, kernel_open);
    //cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_CLOSE, kernel_close);
    //cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_OPEN, kernel_open);
}

void ArmorDetector::getEndpoints(const RotatedRect& rect, Point2f& top, Point2f& bottom) const {
    Point2f pts[4];
    rect.points(pts);
    sort(pts, pts+4, [](const Point2f& a, const Point2f& b) { return a.y < b.y; });
    top = (pts[0] + pts[1]) / 2.0f;
    bottom = (pts[2] + pts[3]) / 2.0f;
}
//灯条候选
vector<ArmorDetector::LightBar> ArmorDetector::extractLights(const Mat& mask, int color_flag) const {
    vector<vector<Point>> contours;
    findContours(mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    vector<LightBar> lights;
    for (auto& cnt : contours) {
        double area = contourArea(cnt);
        if (area < 5 || area > 1000) continue;
        
        RotatedRect r = minAreaRect(cnt);
        float w = r.size.width, h = r.size.height;
        if (h < w) swap(h, w);
        float ratio = h / w;
        if (ratio < 2.0 || ratio > 20) continue;
        
        float ang = fabs(r.angle);
        if (!((ang < 20) || (ang > 70 && ang < 110))) continue;
        
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
//灯条配对
vector<ArmorResult> ArmorDetector::pairLights(const vector<LightBar>& lights) const {
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

            if (ang_diff > 30) continue;
            if (h_diff > 0.6) continue;
            if (dy_ratio > 0.8) continue;
            if (dx_ratio < 0.8 || dx_ratio > 5.0) continue;
            
            const LightBar* left = (l1.center.x < l2.center.x) ? &l1 : &l2;
            const LightBar* right = (l1.center.x < l2.center.x) ? &l2 : &l1;
            
            Point2f left_top, left_bottom, right_top, right_bottom;
            getEndpoints(left->rect, left_top, left_bottom);
            getEndpoints(right->rect, right_top, right_bottom);
            
            float armor_width = right_top.x - left_top.x;
            float armor_height = left_bottom.y - left_top.y;
            if (armor_height > 0 && (armor_width / armor_height < 0.7 || armor_width / armor_height > 3.5)) continue;
            
            ArmorResult res;
            res.points = {left_top, right_top, right_bottom, left_bottom};
            res.color = l1.color;
            armors.push_back(res);
        }
    }
    return armors;
}

vector<ArmorResult> ArmorDetector::detect(const Mat& bgr_frame, DebugInfo* debug) {
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
    
    vector<ArmorResult> result = pairLights(all_lights);
    
    // 如果需要保存配对信息到 debug，可以根据 result 和灯条列表构建，但略复杂，此处先不实现
    return result;
}