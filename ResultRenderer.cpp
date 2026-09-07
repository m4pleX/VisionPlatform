#include "ResultRenderer.h"

#include "GeomPrimitive.h"

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsPathItem>
#include <QPolygonF>
#include <QPainterPath>

#include <QtMath>

ResultRenderer::ResultRenderer(QGraphicsScene* scene)
	: m_scene(scene)
{
}

void ResultRenderer::clear()
{
	for (QGraphicsItem* item : m_items)
	{
		m_scene->removeItem(item);
		delete item;
	}
	m_items.clear();
}

void ResultRenderer::render(const QList<AlgorithmResult>& results)
{
	clear();

	for (const AlgorithmResult& r : results)
	{
		for (const DetectionBox& box : r.detections)
			drawDetection(box);
		for (const PoseResult& pose : r.poses)
			drawPose(pose);
		for (const ResultGeom& geom : r.geometry)
			drawGeometry(geom);
		// masks（SegmentationMask）留待 UNet 落地时补充。
	}
}

/*  检测框：红框 + 上方标签（class_label 置信度） */
void ResultRenderer::drawDetection(const DetectionBox& box)
{
	QPen pen(QColor(255, 60, 60));
	pen.setWidth(2);
	pen.setCosmetic(true);

	QRectF r(box.cx - box.w / 2.0, box.cy - box.h / 2.0, box.w, box.h);
	auto* rect = m_scene->addRect(r, pen, QBrush(Qt::NoBrush));
	rect->setZValue(85);
	m_items.append(rect);

	auto* label = m_scene->addSimpleText(
		QStringLiteral("%1 %.2f").arg(box.label).arg(box.confidence));
	label->setPos(r.left(), r.top() - 18);
	label->setBrush(QBrush(QColor(255, 60, 60)));
	label->setZValue(86);
	m_items.append(label);
}

/*  定位结果：绿中心十字 + 方向线 + 标签 */
void ResultRenderer::drawPose(const PoseResult& poseRes)
{
	const Pose2D& pose = poseRes.pose;

	QPen pen(QColor(0, 170, 90));
	pen.setWidth(2);
	pen.setCosmetic(true);

	// 中心十字
	const double cs = 12.0;
	QLineF h(pose.tx - cs, pose.ty, pose.tx + cs, pose.ty);
	QLineF v(pose.tx, pose.ty - cs, pose.tx, pose.ty + cs);
	auto* hItem = m_scene->addLine(h, pen);
	auto* vItem = m_scene->addLine(v, pen);
	hItem->setZValue(87);
	vItem->setZValue(87);
	m_items.append(hItem);
	m_items.append(vItem);

	// 方向线（angle 为弧度）
	const double len = 60.0;
	QPointF dir(pose.tx + len * qCos(pose.angle), pose.ty + len * qSin(pose.angle));
	auto* dirItem = m_scene->addLine(QLineF(QPointF(pose.tx, pose.ty), dir), pen);
	dirItem->setZValue(87);
	m_items.append(dirItem);

	// 标签
	auto* label = m_scene->addSimpleText(
		QStringLiteral("%1 (%.1f, %.1f) %.2f°").arg(poseRes.label.isEmpty() ? QStringLiteral("pose") : poseRes.label)
			.arg(pose.tx).arg(pose.ty).arg(qRadiansToDegrees(pose.angle)));
	label->setPos(pose.tx + 8, pose.ty + 8);
	label->setBrush(QBrush(QColor(0, 170, 90)));
	label->setZValue(88);
	m_items.append(label);
}

/*  几何结果：按 GeomPrimitive 类型分派（线段/圆/散点） */
void ResultRenderer::drawGeometry(const ResultGeom& result)
{
	const GeomPrimitive& g = result.geom;

	switch (g.type)
	{
	case Geom_Segment:
	case Geom_Line:
	{
		// 线段：起终点连线；Line（无限长）按基点+方向角画一段代表性线段
		QPen pen(QColor(255, 60, 60));
		pen.setWidth(2);
		pen.setCosmetic(true);
		QPointF p1, p2;
		if (g.type == Geom_Segment)
		{
			p1 = QPointF(g.cx, g.cy);
			p2 = QPointF(g.ex, g.ey);
		}
		else
		{
			// Line：向 angle 方向延伸一段（长度 200 仅作可视）
			const double len = 200.0;
			p1 = QPointF(g.cx - len * qCos(g.angle), g.cy - len * qSin(g.angle));
			p2 = QPointF(g.cx + len * qCos(g.angle), g.cy + len * qSin(g.angle));
		}
		auto* ln = m_scene->addLine(QLineF(p1, p2), pen);
		ln->setZValue(88);
		m_items.append(ln);
		break;
	}
	case Geom_Circle:
	{
		QPen pen(QColor(255, 60, 60));
		pen.setWidth(2);
		pen.setCosmetic(true);
		auto* c = m_scene->addEllipse(g.cx - g.r, g.cy - g.r, g.r * 2, g.r * 2, pen, QBrush(Qt::NoBrush));
		c->setZValue(88);
		m_items.append(c);
		break;
	}
	case Geom_Contour:
	case Geom_Point:
	{
		// 散点：小十字标记（Contour 逐点 / Point 单点）
		QPen pen(QColor(0, 170, 90));
		pen.setWidth(1);
		pen.setCosmetic(true);
		QList<QPointF> pts;
		if (g.type == Geom_Contour)
			for (const QPointF& p : g.contour) pts.append(p);
		else
			pts.append(QPointF(g.cx, g.cy));

		for (const QPointF& p : pts)
		{
			auto* ph = m_scene->addLine(QLineF(p.x() - 3, p.y(), p.x() + 3, p.y()), pen);
			auto* pv = m_scene->addLine(QLineF(p.x(), p.y() - 3, p.x(), p.y() + 3), pen);
			ph->setZValue(88);
			pv->setZValue(88);
			m_items.append(ph);
			m_items.append(pv);
		}
		break;
	}
	default:
		// Arc / Ellipse 等其它几何，暂不专设画法（按需补充，结构已留分支）
		break;
	}
}
