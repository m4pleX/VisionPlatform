#include "VisionTools.h"

#include "PoseLocator.h"
#include "GrayDefectDetector.h"
#include "CaliperDetector.h"
#include "ShapeGeometry.h"

/* ===== 定位：blob 定位 ===== */
ToolResult LocateTool::run(const ToolContext& ctx)
{
	ToolResult r;
	if (!ctx.image || ctx.image->empty())
	{
		r.ok = false;
		r.error = QStringLiteral("empty image");
		return r;
	}
	r.data = PoseLocator::locate(*ctx.image);
	return r;
}

/* ===== 检测：灰度缺陷 ===== */
ToolResult GrayDefectTool::run(const ToolContext& ctx)
{
	ToolResult r;
	if (!ctx.image || ctx.image->empty())
	{
		r.ok = false;
		r.error = QStringLiteral("empty image");
		return r;
	}

	// 阶段1：若用户画了矩形 ROI，则在 ROI 子图内检测；否则整图检测。
	// 这验证「人工几何进入算法」的链路，坐标回贴到整图。
	cv::Mat sub = *ctx.image;
	QPoint roiOrigin(0, 0);
	if (!ctx.shapes.isEmpty())
	{
		// 找第一个矩形 ROI 作为检测区域
		for (const DrawShapeItem& s : ctx.shapes)
		{
			if (s.type != Shape_Rect)
				continue;
			QRect roi;
			if (ShapeGeometry::cropRect(s, ctx.image->cols, ctx.image->rows, roi))
			{
				sub = (*ctx.image)(cv::Rect(roi.x(), roi.y(), roi.width(), roi.height())).clone();
				roiOrigin = QPoint(roi.x(), roi.y());
			}
			break;
		}
	}

	r.data = GrayDefectDetector::detect(sub);

	// 坐标回贴：ROI 子图局部坐标 -> 整图坐标
	for (DetectionBox& box : r.data.detections)
	{
		box.cx += roiOrigin.x();
		box.cy += roiOrigin.y();
	}
	return r;
}

/* ===== 测量：卡尺直线 ===== */
ToolResult CaliperTool::run(const ToolContext& ctx)
{
	ToolResult r;
	if (!ctx.image || ctx.image->empty())
	{
		r.ok = false;
		r.error = QStringLiteral("empty image");
		return r;
	}

	const float cx = static_cast<float>(ctx.image->cols) / 2.0f;
	const float cy0 = static_cast<float>(ctx.image->rows) * 0.1f;
	const float cy1 = static_cast<float>(ctx.image->rows) * 0.9f;

	cv::Point2f p1(
		static_cast<float>(m_params.value("p1x").toDouble(cx)),
		static_cast<float>(m_params.value("p1y").toDouble(cy0)));
	cv::Point2f p2(
		static_cast<float>(m_params.value("p2x").toDouble(cx)),
		static_cast<float>(m_params.value("p2y").toDouble(cy1)));

	r.data = CaliperDetector::findLine(*ctx.image, p1, p2);
	return r;
}

/* ===== 工厂 ===== */
ITool* createToolByName(const QString& name)
{
	if (name == "blobLocator")  return new LocateTool();
	if (name == "grayDefect")   return new GrayDefectTool();
	if (name == "caliperLine")  return new CaliperTool();
	return nullptr;
}
