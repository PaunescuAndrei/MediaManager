#pragma once
#include <opencv2\opencv.hpp>
#include <opencv2\photo.hpp>
#include <QString>
#include <QPair>
#include <QColor>
#include <QPixmap>

struct lessVec4b
{
    bool operator()(const cv::Vec4b& lhs, const cv::Vec4b& rhs) const {
        return (lhs[0] != rhs[0]) ? (lhs[0] < rhs[0]) : ((lhs[1] != rhs[1]) ? (lhs[1] < rhs[1]) : ((lhs[2] != rhs[2]) ? (lhs[2] < rhs[2]) : (lhs[3] < rhs[3])));
    }
};

struct color_area {
    QColor color;
    float area_percent = 0;
};
bool less_color_area(const color_area& first, const color_area& second);
bool greater_color_area(const color_area& first, const color_area& second);

void reduceColor_kmeans_old(const cv::Mat& src, cv::Mat& dst);
void reduceColor_kmeans(const cv::Mat& src, cv::Mat& dst, int K_clusters = 10);
QPair<QList<color_area>, QList<color_area>> getPalette(const cv::Mat& src, int max_area);
QPair<QList<color_area>, QList<color_area>> palette_extractor(QPixmap& img, int K_clusters = 10, int target_size = 0);
QPair<QPair<QList<color_area>, QList<color_area>>, cv::Mat> palette_extractor_with_image(QPixmap& img, int K_clusters = 10,int target_size = 0);