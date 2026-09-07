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

	// 【显式 ROI 绑定】按 ctx.roiIds 精确匹配"该工具消费的 ROI"，不再「找第一个矩形」。
	// ctx.roiIds 来自 InspectionItem::roiIds（检测项声明"该算法用哪些 ROI"）。
	// 命中第一个 id 匹配的矩形 ROI 即裁剪；未声明/未命中则回退整图。
	cv::Mat sub = *ctx.image;
	QPoint roiOrigin(0, 0);
	for (const QString& roiId : ctx.roiIds)
	{
		bool found = false;
		for (const DrawShapeItem& s : ctx.shapes)
		{
			if (s.id != roiId || s.type != Shape_Rect)
				continue;
			QRect roi;
			if (ShapeGeometry::cropRect(s, ctx.image->cols, ctx.image->rows, roi))
			{
				sub = (*ctx.image)(cv::Rect(roi.x(), roi.y(), roi.width(), roi.height())).clone();
				roiOrigin = QPoint(roi.x(), roi.y());
				found = true;
			}
			break;
		}
		if (found) break;
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
