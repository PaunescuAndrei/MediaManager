#pragma once
#ifndef ASM_OPENCV_H
#define ASM_OPENCV_H

/**
   Functions to convert between OpenCV's cv::Mat and Qt's QImage and QPixmap.

   Andy Maloney <asmaloney@gmail.com>
   https://asmaloney.com/2013/11/code/converting-between-cvmat-and-qimage-or-qpixmap
**/

#include <QDebug>
#include <QImage>
#include <QPixmap>
#include <QtGlobal>

#include "opencv2/imgproc/imgproc.hpp"

/*
   Endianness
   ---
   We assume Little Endian.
*/
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
#error Some of QImage's formats are endian-dependent. This file assumes little endian.
#endif

namespace ASM {
    [[nodiscard]] inline QImage cvMatToQImage(const cv::Mat& inMat)
    {
        switch (inMat.type())
        {
        // 8-bit, 4 channel (BGRA layout typically in OpenCV)
        case CV_8UC4:
        {
            // Format_ARGB32 matches CV_8UC4 (BGRA) layout on little-endian platforms.
            return QImage(inMat.data,
                          inMat.cols, inMat.rows,
                          static_cast<int>(inMat.step),
                          QImage::Format_ARGB32);
        }

        // 8-bit, 3 channel (BGR layout in OpenCV)
        case CV_8UC3:
        {
            // Format_BGR888 matches CV_8UC3 exactly.
            // This is a direct, zero-copy, and zero-swapping representation of the cv::Mat!
            return QImage(inMat.data,
                          inMat.cols, inMat.rows,
                          static_cast<int>(inMat.step),
                          QImage::Format_BGR888);
        }

        // 8-bit, 1 channel (Grayscale in OpenCV)
        case CV_8UC1:
        {
            return QImage(inMat.data,
                          inMat.cols, inMat.rows,
                          static_cast<int>(inMat.step),
                          QImage::Format_Grayscale8);
        }

        default:
            qWarning() << "ASM::cvMatToQImage() - cv::Mat image type not handled in switch:" << inMat.type();
            break;
        }

        return QImage();
    }

    [[nodiscard]] inline QPixmap cvMatToQPixmap(const cv::Mat& inMat)
    {
        return QPixmap::fromImage(cvMatToQImage(inMat).copy());
    }

    // If inImage exists for the lifetime of the resulting cv::Mat, pass false to inCloneImageData to share inImage's
    // data with the cv::Mat directly.
    [[nodiscard]] inline cv::Mat QImageToCvMat(const QImage& inImage, bool inCloneImageData = true)
    {
        switch (inImage.format())
        {
        // 8-bit, 4 channel
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied:
        {
            cv::Mat mat(inImage.height(), inImage.width(),
                        CV_8UC4,
                        const_cast<uchar*>(inImage.bits()),
                        static_cast<size_t>(inImage.bytesPerLine())
            );

            return (inCloneImageData ? mat.clone() : mat);
        }

        // 8-bit, 3 channel (BGR)
        case QImage::Format_BGR888:
        {
            cv::Mat mat(inImage.height(), inImage.width(),
                        CV_8UC3,
                        const_cast<uchar*>(inImage.bits()),
                        static_cast<size_t>(inImage.bytesPerLine())
            );

            return (inCloneImageData ? mat.clone() : mat);
        }

        // 8-bit, 4 channel (RGBA)
        case QImage::Format_RGBA8888:
        {
            cv::Mat mat(inImage.height(), inImage.width(),
                        CV_8UC4,
                        const_cast<uchar*>(inImage.bits()),
                        static_cast<size_t>(inImage.bytesPerLine())
            );

            cv::Mat matBGRA;
            cv::cvtColor(mat, matBGRA, cv::COLOR_RGBA2BGRA);
            return matBGRA;
        }

        // 8-bit, 3 channel (RGBX) -> drop alpha and swap to BGR
        case QImage::Format_RGB32:
        {
            cv::Mat mat(inImage.height(), inImage.width(),
                        CV_8UC4,
                        const_cast<uchar*>(inImage.bits()),
                        static_cast<size_t>(inImage.bytesPerLine())
            );

            cv::Mat matNoAlpha;
            cv::cvtColor(mat, matNoAlpha, cv::COLOR_BGRA2BGR); // drop the alpha and convert BGRA to BGR
            return matNoAlpha;
        }

        // 8-bit, 3 channel (RGB) -> convert to BGR via highly optimized OpenCV SIMD cvtColor
        case QImage::Format_RGB888:
        {
            cv::Mat mat(inImage.height(), inImage.width(),
                        CV_8UC3,
                        const_cast<uchar*>(inImage.bits()),
                        static_cast<size_t>(inImage.bytesPerLine())
            );

            cv::Mat matBGR;
            cv::cvtColor(mat, matBGR, cv::COLOR_RGB2BGR);
            return matBGR;
        }

        // 8-bit, 1 channel
        case QImage::Format_Indexed8:
        case QImage::Format_Grayscale8:
        {
            cv::Mat mat(inImage.height(), inImage.width(),
                        CV_8UC1,
                        const_cast<uchar*>(inImage.bits()),
                        static_cast<size_t>(inImage.bytesPerLine())
            );

            return (inCloneImageData ? mat.clone() : mat);
        }

        default:
            qWarning() << "ASM::QImageToCvMat() - QImage format not handled in switch:" << inImage.format();
            break;
        }

        return cv::Mat();
    }

    [[nodiscard]] inline cv::Mat QPixmapToCvMat(const QPixmap& inPixmap, bool inCloneImageData = true)
    {
        return QImageToCvMat(inPixmap.toImage(), inCloneImageData);
    }
}

#endif
