#include <opencv2/opencv.hpp>
#include <iostream>

// 过曝提取函数
cv::Mat split_overexpose(const cv::Mat& hsv) {
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    cv::Mat v_channel = channels[2];
    cv::Mat v_mask;
    cv::threshold(v_channel, v_mask, 220, 255, cv::THRESH_BINARY);
    // 闭运算（5x5）连接断裂
    cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5,5));
    cv::morphologyEx(v_mask, v_mask, cv::MORPH_CLOSE, kernel_close);
    // 开运算（2x2）去噪点
    cv::Mat kernel_open = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2,2));
    cv::morphologyEx(v_mask, v_mask, cv::MORPH_OPEN, kernel_open);
    return v_mask;
}

int main() {
    cv::Mat img = cv::imread("../images/armor.jpg");
    if (img.empty()) { std::cout << "无法读取图像" << std::endl; return -1; }

    // Gamma 矫正（gamma=2.0)
    cv::Mat gamma_corrected;
    img.convertTo(gamma_corrected, CV_32F, 1.0/255.0);
    cv::pow(gamma_corrected, 2.0, gamma_corrected);
    gamma_corrected.convertTo(gamma_corrected, CV_8U, 255.0);

    // 转为 HSV
    cv::Mat hsv;
    cv::cvtColor(gamma_corrected, hsv, cv::COLOR_BGR2HSV);

    // 颜色分离（红色）
    cv::Mat red_mask1, red_mask2;
    cv::inRange(hsv, cv::Scalar(0, 0, 80), cv::Scalar(60, 255, 255), red_mask1);
    cv::inRange(hsv, cv::Scalar(170, 0, 80), cv::Scalar(180, 255, 255), red_mask2);
    cv::Mat red_color = red_mask1 | red_mask2;

    // 颜色分离（蓝色）
    cv::Mat blue_color;
    cv::inRange(hsv, cv::Scalar(60, 0, 140), cv::Scalar(110, 255, 255), blue_color);

    // 过曝提取 V 通道 mask
    cv::Mat v_mask = split_overexpose(hsv);

    // 与 V 高亮取交集，得到更完整的灯条区域
    cv::Mat red_mask = red_color & v_mask;
    cv::Mat blue_mask = blue_color & v_mask;
    //cv::Mat red_mask = red_color;
    //cv::Mat blue_mask = blue_color;
    
    // 形态学处理(先闭后开)
    cv::Mat kernel_close = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::Mat kernel_open = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));

    cv::morphologyEx(red_mask, red_mask, cv::MORPH_CLOSE, kernel_close);
    cv::morphologyEx(red_mask, red_mask, cv::MORPH_OPEN, kernel_open);
    cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_CLOSE, kernel_close);
    cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_OPEN, kernel_open);

    cv::imwrite("../output/red_mask.png", red_mask);
    cv::imwrite("../output/blue_mask.png", blue_mask);
    std::cout << "已生成新的 mask" << std::endl;
    return 0;
}