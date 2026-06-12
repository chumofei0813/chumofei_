#include "armor_detector/NumberClassifier.h"

const char* NumberClassifier::CLASS_NAMES[9] = {
    "one", "two", "three", "four", "five",
    "sentry", "outpost", "base", "not_armor"
};

NumberClassifier::NumberClassifier(const std::string& model_path, float conf_thresh)
    : conf_thresh_(conf_thresh)
{
    net_ = cv::dnn::readNetFromONNX(model_path);
}

cv::Mat NumberClassifier::extractArmorROI(const cv::Mat& frame,
                                          const cv::Point2f points[4])
{
    // points 顺序: TL, TR, BR, BL（灯条端点）
    //
    // 裁剪策略：
    //   1. 沿装甲板竖直方向上下外扩角点（数字/图案区域超出灯条范围）
    //   2. 透视变换拉正
    //   3. 裁掉左右灯条区域，保留中间数字/图案

    // 计算装甲板竖直方向
    cv::Point2f down_dir = (points[3] - points[0]) + (points[2] - points[1]);
    float down_len = cv::norm(down_dir);
    if (down_len < 1e-6f) return cv::Mat();
    down_dir /= down_len;
    cv::Point2f up_dir = -down_dir;

    // 沿竖直方向上下外扩
    float left_h  = cv::norm(points[3] - points[0]);
    float right_h = cv::norm(points[2] - points[1]);
    float extend  = (left_h + right_h) * 0.5f * 0.5f;  // 灯条平均高度 × 50%

    cv::Point2f ext_pts[4];
    ext_pts[0] = points[0] + up_dir * extend;     // TL
    ext_pts[1] = points[1] + up_dir * extend;     // TR
    ext_pts[2] = points[2] + down_dir * extend;   // BR
    ext_pts[3] = points[3] + down_dir * extend;   // BL

    // 透视变换
    float top_w   = cv::norm(ext_pts[1] - ext_pts[0]);
    float bottom_w = cv::norm(ext_pts[2] - ext_pts[3]);
    float ext_left_h  = cv::norm(ext_pts[3] - ext_pts[0]);
    float ext_right_h = cv::norm(ext_pts[2] - ext_pts[1]);

    int dst_w = static_cast<int>(std::max(top_w, bottom_w));
    int dst_h = static_cast<int>(std::max(ext_left_h, ext_right_h));

    if (dst_w <= 0 || dst_h <= 0) return cv::Mat();

    cv::Point2f dst_pts[4] = {
        cv::Point2f(0, 0),
        cv::Point2f(static_cast<float>(dst_w - 1), 0),
        cv::Point2f(static_cast<float>(dst_w - 1), static_cast<float>(dst_h - 1)),
        cv::Point2f(0, static_cast<float>(dst_h - 1))
    };

    cv::Mat M = cv::getPerspectiveTransform(ext_pts, dst_pts);
    cv::Mat roi;
    cv::warpPerspective(frame, roi, M, cv::Size(dst_w, dst_h));

    // 裁掉左右灯条区域，保留中间数字/图案
    int trim = static_cast<int>(dst_w * 0.15);
    if (trim > 0 && trim * 2 < dst_w) {
        roi = roi(cv::Rect(trim, 0, dst_w - 2 * trim, dst_h));
    }

    return roi;
}

cv::Mat NumberClassifier::preprocess(const cv::Mat& gray_roi) const
{
    // 直接拉伸到 32×32（数字应填满画布）
    cv::Mat resized;
    cv::resize(gray_roi, resized, cv::Size(32, 32), 0, 0, cv::INTER_LINEAR);

    // [0, 1] 归一化 + NCHW：单通道灰度，不需要 swapRB
    return cv::dnn::blobFromImage(resized, 1.0 / 255.0, cv::Size(), cv::Scalar(), false);
}

ClassifyResult NumberClassifier::classify(const cv::Mat& roi_bgr)
{
    ClassifyResult result;

    if (roi_bgr.empty()) return result;

    // BGR → Gray
    cv::Mat gray;
    if (roi_bgr.channels() == 3)
        cv::cvtColor(roi_bgr, gray, cv::COLOR_BGR2GRAY);
    else
        gray = roi_bgr;

    // 预处理为 (1,1,32,32) blob
    cv::Mat blob = preprocess(gray);
    if (blob.empty()) return result;

    // 推理
    net_.setInput(blob);
    cv::Mat output = net_.forward();   // shape: (1, 9) 或 (9,)

    // 展平为 1×9 float
    cv::Mat flat;
    output.reshape(1, 1).convertTo(flat, CV_32F);

    // softmax：exp(x_i - max) / Σ exp(x_j - max)
    double max_logit;
    cv::minMaxLoc(flat, nullptr, &max_logit);
    cv::Mat softmaxed;
    cv::exp(flat - max_logit, softmaxed);
    softmaxed /= cv::sum(softmaxed)[0];

    // 取 argmax
    cv::Point max_loc;
    double max_val;
    cv::minMaxLoc(softmaxed, nullptr, &max_val, nullptr, &max_loc);

    result.class_id = max_loc.x;
    result.confidence = static_cast<float>(max_val);

    // 置信度低于阈值，视为不可靠
    if (result.confidence < conf_thresh_) {
        result.class_id = -1;
        result.class_name = "unknown";
        return result;
    }

    if (result.class_id >= 0 && result.class_id < 9)
        result.class_name = CLASS_NAMES[result.class_id];
    else
        result.class_name = "unknown";

    return result;
}
