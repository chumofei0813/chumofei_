#ifndef NUMBER_CLASSIFIER_H
#define NUMBER_CLASSIFIER_H

#include <opencv2/opencv.hpp>
#include <string>

// 数字分类结果
struct ClassifyResult {
    int class_id = -1;          // 0-8，-1 表示推理失败
    float confidence = 0.0f;
    std::string class_name;     // "one", "two", ..., "not_armor"
};

// 基于 ONNX 的装甲板数字分类器
class NumberClassifier {
public:
    // 类别映射：0=one, 1=two, 2=three, 3=four, 4=five,
    //           5=sentry, 6=outpost, 7=base, 8=not_armor
    static const char* CLASS_NAMES[9];

    // model_path: ONNX 模型文件路径
    // conf_thresh: 置信度阈值，低于此值结果不可靠
    NumberClassifier(const std::string& model_path, float conf_thresh = 0.5f);

    // 对装甲板 ROI（BGR 图）做数字分类
    ClassifyResult classify(const cv::Mat& roi_bgr);

    // 从原图中用 4 个角点透视变换提取装甲板 ROI
    // points 顺序：左上, 右上, 右下, 左下
    static cv::Mat extractArmorROI(const cv::Mat& frame,
                                   const cv::Point2f points[4]);

private:
    cv::dnn::Net net_;
    float conf_thresh_;

    // 预处理：灰度 → 拉伸到 32×32 → [0,1] 归一化 → NCHW blob
    cv::Mat preprocess(const cv::Mat& gray_roi) const;
};

#endif
