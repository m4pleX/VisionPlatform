/*
 * 文件名：CvImageConverter.h
 * 职责：图像载入 / Qt 图像与 OpenCV 图像互转工具（纯静态）
 *
 * 核心功能：
 *   - loadImage(path)：cv::imread 直读文件 → cv::Mat（主表示，不进 QImage 中转）
 *   - toCvMat(QImage)：QImage → cv::Mat：兼容旧路径（结果回显/参数面板缓存图）
 *   - toQImage(mat)：  cv::Mat → QImage：渲染上屏桥接（显示时 BGR→RGB）
 *
 * 【流通票契约】（对齐 ImageSource.h 的"ImageData 为流通票、cv::Mat 为存储后端"）：
 *   - cv::Mat 是主表示；QImage 仅作【渲染上屏】那一刻的显示桥接。
 *   - 加载【不要】走 QImage 中转（loadImage 用 imread 直读 cv::Mat），
 *     避免 QImage → cv::Mat 的多余深拷贝。
 *   - BGR/RGB 通道差异【只在显示时】处理（toQImage 内 cvtColor），
 *     不在桥接层偷偷重排误导调用方。
 *
 * 设计约束：
 *   - toQImage 一律【深拷贝】出独立 QImage（后台 Mat 下次会被覆盖，显示必须独立副本）；
 *     灰度单通道默认可零拷贝，但为生命周期安全统一走深拷贝，代价 O(n) 可忽略。
 *   - 仅处理 8bit 三通道（BGR ↔ RGB888）与灰度（Gray ↔ CV_8UC1），
 *     满足检测/显示场景，不做完整格式矩阵。
 */
#pragma once

#include <QString>
#include <QImage>

namespace cv { class Mat; }

class CvImageConverter
{
public:
	/*  文件 → cv::Mat（cv::imread 直读，主表示；失败返回空 Mat） */
	static cv::Mat loadImage(const QString& path);

	/*  QImage → cv::Mat（深拷贝；兼容旧路径，如结果回显缓存图为 QImage 时） */
	static cv::Mat toCvMat(const QImage& image);

	/*  cv::Mat(BGR 或 灰度) → QImage（深拷贝；彩色图内部 cvtColor BGR→RGB 供显示） */
	static QImage toQImage(const cv::Mat& mat);
};
