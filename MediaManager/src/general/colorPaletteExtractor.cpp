#include "stdafx.h"
#include "colorPaletteExtractor.h"
#include <QDebug>
#include "asmOpenCV.h"
#include <unordered_map>
#include <vector>
#include <cstring>

// https://stackoverflow.com/a/34734939/5008845
void reduceColor_kmeans_old(const cv::Mat& src, cv::Mat& dst)
{
    int K = 6;
    int n = src.rows * src.cols;
    cv::Mat data = src.reshape(1, n);
    data.convertTo(data, CV_32F);

    std::vector<int> labels;
    cv::Mat1f colors;
    kmeans(data, K, labels, cv::TermCriteria(), 1, cv::KMEANS_PP_CENTERS, colors);

    for (int i = 0; i < n; ++i)
        for (int j = 0; j < src.channels(); ++j)
            data.at<float>(i, j) = colors(labels[i], j);

    cv::Mat reduced = data.reshape(src.channels(), src.rows);
    reduced.convertTo(dst, src.type());
}

void reduceColor_kmeans(const cv::Mat& src, cv::Mat& dst, int K_clusters)
{
    if (src.empty()) {
        dst = src.clone();
        return;
    }

    std::vector<cv::Vec4b> points;
    std::vector<cv::Point> locations;
    points.reserve(src.rows * src.cols);
    locations.reserve(src.rows * src.cols);

    // Fast pointer-based traversal to gather non-transparent pixels
    for (int y = 0; y < src.rows; ++y) {
        const cv::Vec4b* rowPtr = src.ptr<cv::Vec4b>(y);
        for (int x = 0; x < src.cols; ++x) {
            if (rowPtr[x][3] != 0) { // Alpha is non-zero (non-transparent)
                points.push_back(rowPtr[x]);
                locations.push_back(cv::Point(x, y));
            }
        }
    }

    if (points.empty()) {
        dst = src.clone();
        return;
    }

    cv::Mat kmeanPoints(static_cast<int>(points.size()), 4, CV_32F);
    for (size_t y = 0; y < points.size(); ++y) {
        float* ptr = kmeanPoints.ptr<float>(static_cast<int>(y));
        ptr[0] = static_cast<float>(points[y][0]);
        ptr[1] = static_cast<float>(points[y][1]);
        ptr[2] = static_cast<float>(points[y][2]);
        ptr[3] = static_cast<float>(points[y][3]);
    }

    cv::Mat labels;
    cv::Mat centers;
    cv::TermCriteria criteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 10, 1.0);
    int actual_clusters = std::min(K_clusters, static_cast<int>(points.size()));
    cv::kmeans(kmeanPoints, actual_clusters, labels, criteria, 3, cv::KMEANS_PP_CENTERS, centers);

    dst = src.clone(); // deep copy to ensure we own and don't alias source
    cv::Vec4b tempColor;
    for (size_t i = 0; i < locations.size(); ++i) {
        int cluster_idx = labels.at<int>(static_cast<int>(i), 0);
        float* centerPtr = centers.ptr<float>(cluster_idx);
        
        cv::Vec4b& dstPixel = dst.at<cv::Vec4b>(locations[i]);
        dstPixel[0] = static_cast<uchar>(centerPtr[0]);
        dstPixel[1] = static_cast<uchar>(centerPtr[1]);
        dstPixel[2] = static_cast<uchar>(centerPtr[2]);
        dstPixel[3] = static_cast<uchar>(centerPtr[3]);
    }
}

bool less_color_area(const color_area& first, const color_area& second)
{
    return first.area_percent < second.area_percent;
}

bool greater_color_area(const color_area& first, const color_area& second)
{
    return first.area_percent > second.area_percent;
}

QPair<QList<color_area>, QList<color_area>> getPalette(const cv::Mat& src, int max_area)
{
    // Fast O(1) unordered_map hash table
    std::unordered_map<uint32_t, int> palette;

    for (int r = 0; r < src.rows; ++r)
    {
        const cv::Vec4b* rowPtr = src.ptr<cv::Vec4b>(r);
        for (int c = 0; c < src.cols; ++c)
        {
            cv::Vec4b color = rowPtr[c];
            if (color[3] != 0) { // Non-transparent
                uint32_t rgba;
                std::memcpy(&rgba, &color, sizeof(uint32_t));
                palette[rgba]++;
            }
        }
    }

    QList<color_area> accepted_colors;
    QList<color_area> rejected_colors;
    accepted_colors.reserve(static_cast<qsizetype>(palette.size()));
    rejected_colors.reserve(static_cast<qsizetype>(palette.size()));

    for (const auto& [rgba, count] : palette) {
        cv::Vec4b color_cv;
        std::memcpy(&color_cv, &rgba, sizeof(uint32_t));

        // color_cv layout is BGRA, so index 2 is Red, 1 is Green, 0 is Blue.
        QColor color(color_cv[2], color_cv[1], color_cv[0]);
        float h = color.hsvHue();
        float s = color.hsvSaturationF();
        float l = color.lightnessF();
        int v = color.value();
        float area = static_cast<float>(count) / static_cast<float>(max_area);

        // HSV filtering to separate primary background colors from the core artwork colors
        if (!(l <= 0.25f || l >= 0.95f || s <= 0.15f || (s < 0.20f && l < 0.4f) ||
            (((h > 2.0f && h < 30.0f) || ((h <= 2.0f || h > 350.0f) && ((s <= 0.6f && l <= 0.5f) || (s <= 0.4f && l <= 0.7f))) || (h >= 30.0f && h < 55.0f && l >= 0.88f)) && (v >= 15 && v <= 255)))) {
            accepted_colors.append({ color, area });
        }
        else if (l > 0.25f) {
            rejected_colors.append({ color, area });
        }
    }
    return { accepted_colors, rejected_colors };
}

QPair<QList<color_area>, QList<color_area>> palette_extractor(const QPixmap& img, int K_clusters, int target_size)
{
    cv::Mat img_ = ASM::QPixmapToCvMat(img, true);
    cv::Size img_size = img_.size();
    
    // Scale calculation bug fix (img_size.width > img_size.height instead of width > width)
    double scale = img_size.width > img_size.height ? static_cast<double>(target_size) / img_size.width : static_cast<double>(target_size) / img_size.height;
    if (scale > 0.0)
        cv::resize(img_, img_, cv::Size(0, 0), scale, scale);

    if (img_.channels() == 3)
        cv::cvtColor(img_, img_, cv::COLOR_BGR2BGRA, 4);
    if (img_.channels() == 1)
        cv::cvtColor(img_, img_, cv::COLOR_GRAY2BGRA, 4);
        
    // Reduce color
    cv::Mat reduced;
    reduceColor_kmeans(img_, reduced, K_clusters);
    int max_area = img_.rows * img_.cols;

    return getPalette(reduced, max_area);
}

QPair<QPair<QList<color_area>, QList<color_area>>, cv::Mat> palette_extractor_with_image(const QPixmap& img, int K_clusters, int target_size)
{
    cv::Mat img_ = ASM::QPixmapToCvMat(img, true);
    cv::Size img_size = img_.size();
    
    // Scale calculation bug fix (img_size.width > img_size.height)
    double scale = img_size.width > img_size.height ? static_cast<double>(target_size) / img_size.width : static_cast<double>(target_size) / img_size.height;
    if (scale > 0.0)
        cv::resize(img_, img_, cv::Size(0, 0), scale, scale);

    if (img_.channels() == 3)
        cv::cvtColor(img_, img_, cv::COLOR_BGR2BGRA, 4);
    if (img_.channels() == 1)
        cv::cvtColor(img_, img_, cv::COLOR_GRAY2BGRA, 4);
        
    // Reduce color
    cv::Mat reduced;
    reduceColor_kmeans(img_, reduced, K_clusters);
    int max_area = img_.rows * img_.cols;

    return { getPalette(reduced, max_area), reduced };
}