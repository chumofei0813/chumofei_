#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>

using namespace cv;
using namespace std;

//color_segmentation中的函数
cv::Mat split_overexpose(const cv::Mat& hsv) {
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    cv::Mat v_channel = channels[2];
    cv::Mat v_mask;
    cv::threshold(v_channel, v_mask, 220, 255, cv::THRESH_BINARY);
    cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5,5));
    cv::morphologyEx(v_mask, v_mask, cv::MORPH_CLOSE, kernel_close);
    cv::Mat kernel_open = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2,2));
    cv::morphologyEx(v_mask, v_mask, cv::MORPH_OPEN, kernel_open);
    return v_mask;
}

void getMasks(const cv::Mat& bgr, cv::Mat& red_mask, cv::Mat& blue_mask) {
    // Gamma 矫正
    cv::Mat gamma_corrected;
    bgr.convertTo(gamma_corrected, CV_32F, 1.0/255.0);
    cv::pow(gamma_corrected, 2.0, gamma_corrected);
    gamma_corrected.convertTo(gamma_corrected, CV_8U, 255.0);
    
    // 转hsv
    cv::Mat hsv;
    cv::cvtColor(gamma_corrected, hsv, cv::COLOR_BGR2HSV);

    // 红色 mask
    cv::Mat red_mask1, red_mask2;
    cv::inRange(hsv, cv::Scalar(0, 50, 50), cv::Scalar(30, 255, 255), red_mask1);
    cv::inRange(hsv, cv::Scalar(170, 50, 50), cv::Scalar(180, 255, 255), red_mask2);
    cv::Mat red_color = red_mask1 | red_mask2;

    // 蓝色 mask
    cv::Mat blue_color;
    cv::inRange(hsv, cv::Scalar(70, 120, 50), cv::Scalar(140, 255, 255), blue_color);
    
    // 过曝提取
    cv::Mat v_mask = split_overexpose(hsv);

    red_mask = red_color & v_mask;
    blue_mask = blue_color & v_mask;
    //red_mask = red_color;
    //blue_mask = blue_color;

    //形态学处理
    //cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
    //cv::Mat kernel_open = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3));
    //cv::morphologyEx(red_mask, red_mask, cv::MORPH_CLOSE, kernel_close);
    //cv::morphologyEx(red_mask, red_mask, cv::MORPH_OPEN, kernel_open);
    //cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_CLOSE, kernel_close);
    //cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_OPEN, kernel_open);
}

//single_image_detector中的结构体和函数
struct LightBar {
    RotatedRect rect;
    Point2f center;
    float angle;
    float height;
    float width;
    int color;
};

struct Armor {
    vector<Point2f> pts;
    int color;
};

void getEndpoints(const RotatedRect& rect, Point2f& top, Point2f& bottom) {
    Point2f pts[4];
    rect.points(pts);
    sort(pts, pts+4, [](const Point2f& a, const Point2f& b) { return a.y < b.y; });
    top = (pts[0] + pts[1]) / 2.0f;
    bottom = (pts[2] + pts[3]) / 2.0f;
}

// 灯条候选
vector<LightBar> filterLightBars(const Mat& mask, int color_flag) {
    vector<vector<Point>> contours;
    findContours(mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    vector<LightBar> lights;
    for (auto& cnt : contours) {
        // 面积
        double area = contourArea(cnt);
        if (area < 5 || area > 1000) continue;
        // 确定长宽
        RotatedRect r = minAreaRect(cnt);
        float w = r.size.width, h = r.size.height;
        if (h < w) swap(h, w);
        // 长宽比
        float ratio = h / w;
        if (ratio < 2.0 || ratio > 20) continue;
        // 角度
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

// 灯条配对
vector<Armor> matchLightBars(vector<LightBar>& lights) {
    vector<Armor> armors;
    for (size_t i = 0; i < lights.size(); i++) {
        for (size_t j = i+1; j < lights.size(); j++) {
            LightBar& l1 = lights[i];
            LightBar& l2 = lights[j];
            if (l1.color != l2.color) continue;
            float ang_diff = fabs(l1.angle - l2.angle);                                      //角度差
            float h_diff = fabs(l1.height - l2.height) / max(l1.height, l2.height);          //高度差比例
            float avg_len = (l1.height + l2.height) / 2.0f;                                  
            float dx = fabs(l1.center.x - l2.center.x);                                      //水平中心距
            float dy = fabs(l1.center.y - l2.center.y);                                      //垂直中心距
            float dx_ratio = dx / avg_len;
            float dy_ratio = dy / avg_len;
            if (ang_diff > 30) continue;
            if (h_diff > 0.6) continue;
            if (dy_ratio > 0.8) continue;
            if (dx_ratio < 0.8 || dx_ratio > 5.0) continue;
            
            LightBar* left = (l1.center.x < l2.center.x) ? &l1 : &l2;
            LightBar* right = (l1.center.x < l2.center.x) ? &l2 : &l1;
            Point2f left_top, left_bottom, right_top, right_bottom;
            getEndpoints(left->rect, left_top, left_bottom);
            getEndpoints(right->rect, right_top, right_bottom);
            //装甲板宽高比过滤
            float armor_width = right_top.x - left_top.x;
            float armor_height = left_bottom.y - left_top.y;
            if (armor_height > 0 && (armor_width / armor_height < 0.7 || armor_width / armor_height > 3.5)) continue;
            
            armors.push_back({{left_top, right_top, right_bottom, left_bottom}, l1.color});
        }
    }
    return armors;
}

int main(int argc, char** argv) {

    string video_path;
    if (argc > 1) {
        video_path = argv[1];
    } else {
        video_path = "../videos/Red_1000.mp4";
        cout << "未指定视频，使用默认: " << video_path << endl;
    }

    system("mkdir -p ../output/videos");

    VideoCapture cap(video_path);
    if (!cap.isOpened()) {
        cerr << "无法打开视频: " << video_path << endl;
        return -1;
    }

    size_t pos = video_path.find_last_of('/');
    string filename = (pos == string::npos) ? video_path : video_path.substr(pos+1);
    string out_name = "../output/videos/" + filename;

    double fps = cap.get(CAP_PROP_FPS);
    int width = (int)cap.get(CAP_PROP_FRAME_WIDTH);
    int height = (int)cap.get(CAP_PROP_FRAME_HEIGHT);
    VideoWriter writer(out_name, VideoWriter::fourcc('M','J','P','G'), fps, Size(width, height));
    if (!writer.isOpened()) {
        cerr << "无法创建输出视频: " << out_name << endl;
        return -1;
    }

    Mat frame;
    int frame_idx = 0;
    while (cap.read(frame)) {
        frame_idx++;
        Mat red_mask, blue_mask;
        getMasks(frame, red_mask, blue_mask);
        vector<LightBar> red_lights = filterLightBars(red_mask, 0);
        vector<LightBar> blue_lights = filterLightBars(blue_mask, 1);
        vector<LightBar> all_lights;
        all_lights.insert(all_lights.end(), red_lights.begin(), red_lights.end());
        all_lights.insert(all_lights.end(), blue_lights.begin(), blue_lights.end());
        vector<Armor> armors = matchLightBars(all_lights);
        Mat result = frame.clone();
        for (auto& a : armors) {
            vector<Point> poly;
            for (auto& p : a.pts) poly.push_back(Point(p.x, p.y));
            polylines(result, vector<vector<Point>>{poly}, true,
                      (a.color==0 ? Scalar(0,0,255) : Scalar(255,0,0)), 3);
            putText(result, (a.color==0 ? "RED" : "BLUE"), a.pts[0],
                    FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255,255,255), 2);
        }
        writer.write(result);
        
        //保存第一帧的调试图象，检查mask
        if (frame_idx == 1) {
            imwrite("../output/first_red_mask.png", red_mask);
            imwrite("../output/first_blue_mask.png", blue_mask);
            imwrite("../output/first_frame.png", frame);
        }
    
    }
    cout << "处理完成: " << out_name << " 总帧数: " << frame_idx << endl;
    return 0;
}