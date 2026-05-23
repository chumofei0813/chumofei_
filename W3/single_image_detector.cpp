#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace cv;
using namespace std;

// 灯条结构体
struct LightBar {
    RotatedRect rect;
    Point2f center;
    float angle;
    float height;
    float width;
    int color; // 0=红,1=蓝
};

// 装甲结构体
struct Armor {
    vector<Point2f> pts; // 左上、右上、右下、左下
    int color;
};

// 获取灯条上下端点（按 y 排序）
void getEndpoints(const RotatedRect& rect, Point2f& top, Point2f& bottom) {
    Point2f pts[4];
    rect.points(pts);
    sort(pts, pts+4, [](const Point2f& a, const Point2f& b) { return a.y < b.y; });
    top = (pts[0] + pts[1]) / 2.0f;
    bottom = (pts[2] + pts[3]) / 2.0f;
}

// 筛选灯条
vector<LightBar> filterLightBars(const Mat& mask, int color_flag) {
    vector<vector<Point>> contours;
    findContours(mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    vector<LightBar> lights;

    for (auto& cnt : contours) {
        //面积
        double area = contourArea(cnt);
        if (area < 5 || area > 1000) continue;

        RotatedRect r = minAreaRect(cnt);
        float w = r.size.width;
        float h = r.size.height;
        if (h < w) swap(h, w); // 保证高 > 宽
        //长宽比
        float ratio = h / w;
        if (ratio < 2.0 || ratio > 20) continue;
        //角度
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
        for (size_t j = i + 1; j < lights.size(); j++) {
            LightBar& l1 = lights[i];
            LightBar& l2 = lights[j];
            if (l1.color != l2.color) continue;

            float ang_diff = fabs(l1.angle - l2.angle);
            float h_diff = fabs(l1.height - l2.height) / max(l1.height, l2.height);
            float avg_len = (l1.height + l2.height) / 2.0f;
            float dx = fabs(l1.center.x - l2.center.x);
            float dy = fabs(l1.center.y - l2.center.y);
            float dx_ratio = dx / avg_len;   // 水平距离与平均长度之比
            float dy_ratio = dy / avg_len;   // 垂直偏移与平均长度之比

            if (ang_diff > 30) continue;
            if (h_diff > 0.6) continue;
            if (dy_ratio > 0.8) continue;    // 垂直偏移不超过平均长度的0.8倍（可根据实际收紧到0.6）
            if (dx_ratio < 0.8 || dx_ratio > 5.0) continue;  // 水平距离比例范围
            
            // 确定左右
            LightBar* left = (l1.center.x < l2.center.x) ? &l1 : &l2;
            LightBar* right = (l1.center.x < l2.center.x) ? &l2 : &l1;

            Point2f left_top, left_bottom, right_top, right_bottom;
            getEndpoints(left->rect, left_top, left_bottom);
            getEndpoints(right->rect, right_top, right_bottom);

            //宽高比
            float armor_width = right_top.x - left_top.x;
            float armor_height = left_bottom.y - left_top.y;
            if (armor_width / armor_height < 0.7 || armor_width / armor_height > 3.5) continue;

            //构造装甲板四个有序点
            vector<Point2f> pts;
            pts.push_back(left_top);
            pts.push_back(right_top);
            pts.push_back(right_bottom);
            pts.push_back(left_bottom);

            Armor a;
            a.pts = pts;
            a.color = l1.color;
            armors.push_back(a);
        }
    }
    return armors;
}

int main() {
    // 读取原图
    Mat src = imread("../images/armor.jpg");
    if (src.empty()) {
        cout << "无法读取原图 ../images/armor.jpg" << endl;
        return -1;
    }

    // 读取已经生成的 mask
    Mat red_mask = imread("../output/red_mask.png", IMREAD_GRAYSCALE);
    Mat blue_mask = imread("../output/blue_mask.png", IMREAD_GRAYSCALE);
    if (red_mask.empty() || blue_mask.empty()) {
        cout << "无法读取 mask 请先运行颜色分离程序生成 red_mask.png 和 blue_mask.png" << endl;
        return -1;
    }

    // 筛选灯条
    vector<LightBar> red_lights = filterLightBars(red_mask, 0);
    vector<LightBar> blue_lights = filterLightBars(blue_mask, 1);
    vector<LightBar> all_lights;
    all_lights.insert(all_lights.end(), red_lights.begin(), red_lights.end());
    all_lights.insert(all_lights.end(), blue_lights.begin(), blue_lights.end());

    cout << "红色灯条候选数: " << red_lights.size() << endl;
    cout << "蓝色灯条候选数: " << blue_lights.size() << endl;

    // 绘制灯条候选（中间结果）保存图片
    Mat light_img = src.clone();
    for (auto& lb : all_lights) {
        Scalar c = (lb.color == 0) ? Scalar(0,0,255) : Scalar(255,0,0);
        Point2f pts[4];
        lb.rect.points(pts);
        for (int i = 0; i < 4; ++i)
            line(light_img, pts[i], pts[(i+1)%4], c, 2);
    }
    imwrite("../output/04_lightbars.png", light_img);
    cout << "已保存灯条候选图: ../output/04_lightbars.png" << endl;

    // 配对生成装甲板
    vector<Armor> armors = matchLightBars(all_lights);
    cout << "装甲板候选数: " << armors.size() << endl;

    // 绘制最终结果
    Mat result = src.clone();
    for (auto& a : armors) {
        vector<Point> poly;
        for (auto& p : a.pts)
            poly.push_back(Point(p.x, p.y));
        polylines(result, vector<vector<Point>>{poly}, true,
                  (a.color==0 ? Scalar(0,0,255) : Scalar(255,0,0)), 3);

        cout << "=====================" << endl;
        cout << (a.color==0 ? "红色装甲" : "蓝色装甲") << endl;
        cout << "左上:" << a.pts[0] << endl;
        cout << "右上:" << a.pts[1] << endl;
        cout << "右下:" << a.pts[2] << endl;
        cout << "左下:" << a.pts[3] << endl;

        putText(result, (a.color==0 ? "RED" : "BLUE"), a.pts[0],
                FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255,255,255), 2);
    }
    imwrite("../output/05_final_result.png", result);
    cout << "已保存最终结果: ../output/05_final_result.png" << endl;

    return 0;
}