#include "stdafx.h"
#include "colorPaletteExtractor.h"
#include <QDebug>
#include "asmOpenCV.h"
#include <map>

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
    std::vector<cv::Vec4b> points;
    std::vector<cv::Point> locations;
    for (int y = 0; y < src.rows; y++) {
        for (int x = 0; x < src.cols; x++) {
            if (src.at<cv::Vec4b>(y, x)[3] != 0) {
                points.push_back(src.at<cv::Vec4b>(y, x));
                locations.push_back(cv::Point(x, y));
            }
        }
    }
    cv::Mat kmeanPoints(points.size(), 4, CV_32F);
    for (int y = 0; y < points.size(); y++) {
        for (int z = 0; z < 4; z++) {
            kmeanPoints.at<float>(y, z) = points[y][z];
        }
    }

    cv::Mat labels;
    cv::Mat centers;
    cv::TermCriteria criteria = cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 10, 1.0);
    cv::kmeans(kmeanPoints, K_clusters, labels, criteria, 3, cv::KMEANS_PP_CENTERS, centers);

    dst = cv::Mat(src);
    cv::Vec4b tempColor;
    for (int i = 0; i < locations.size(); i++) {
        int cluster_idx = labels.at<int>(i, 0);
        tempColor[0] = centers.at<float>(cluster_idx, 0);
        tempColor[1] = centers.at<float>(cluster_idx, 1);
        tempColor[2] = centers.at<float>(cluster_idx, 2);
        tempColor[3] = centers.at<float>(cluster_idx, 3);
        dst.at<cv::Vec4b>(locations[i]) = tempColor;
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
    std::map<cv::Vec4b, int, lessVec4b> palette;
    for (int r = 0; r < src.rows; ++r)
    {
        for (int c = 0; c < src.cols; ++c)
        {
            cv::Vec4b color = src.at<cv::Vec4b>(r, c);
            if (color[3] != 0)
                if (!palette.contains(color))
                {
                    palette[color] = 1;
                }
                else
                {
                    palette[color] = palette[color] + 1;
                }
        }
    }

    QList<color_area> accepted_colors = QList<color_area>();
    QList<color_area> rejected_colors = QList<color_area>();

    for (auto& color_cv : palette) {
        QColor color(color_cv.first[2], color_cv.first[1], color_cv.first[0]);
        float h = color.hsvHue();
        float s = color.hsvSaturationF();
        float l = color.lightnessF();
        int v = color.value();
        float area = float(color_cv.second) / max_area;
        //
        if (not (l <= 0.25 or l >= 0.95 or s <= 0.15 or (s < 0.20 and l < 0.4) or
            (((h > 2 and h < 30) or ((h <= 2 or h > 350) and ((s <= 0.6 and l <= 0.5) or (s <= 0.4 and l <= 0.7))) or (h >= 30 and h < 55 and l >= 0.88)) and (v >= 15 and v <= 255)))) {
            accepted_colors.append({ color,area });
        }
        else if (l > 0.25) {
            rejected_colors.append({ color,area });
        }
    }
    return { accepted_colors,rejected_colors };
}

QPair<QList<color_area>, QList<color_area>> palette_extractor(QPixmap& img, int K_clusters, int target_size)
{
    cv::Mat img_ = ASM::QPixmapToCvMat(img, true);
    cv::Size img_size = img_.size();
    double scale = img_size.width > img_size.width ? float(target_size) / img_size.width : float(target_size) / img_size.height;
    if (scale > 0)
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

QPair<QPair<QList<color_area>, QList<color_area>>, cv::Mat> palette_extractor_with_image(QPixmap& img, int K_clusters, int target_size) {
    cv::Mat img_ = ASM::QPixmapToCvMat(img, true);
    cv::Size img_size = img_.size();
    double scale = img_size.width > img_size.width ? float(target_size) / img_size.width : float(target_size) / img_size.height;
    if (scale > 0)
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