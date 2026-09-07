#include "RecipeIO.h"

#include <QJsonArray>
#include <utility>

static const char* kTypeRect       = "rect";
static const char* kTypeRotatedRect= "rotatedRect";
static const char* kTypeCircle     = "circle";
static const char* kTypeEllipse    = "ellipse";
static const char* kTypeRing       = "ring";
static const char* kTypeArc        = "arc";       /*  扇环 */
static const char* kTypePolygon    = "polygon";

QJsonObject RecipeIO::shapeToJson(const DrawShapeItem& s)
{
	QJsonObject o;
	// 业务元信息（id/label/classId/sourceToolId）：统一写入，不随 type 分支变化；
	// 供多实例标识 / ROI 语义 / 算法类别标注使用。
	// sourceToolId：ROI 来源标记（空=人工绘制；非空=该定位工具 id 生成），空串不写键以保持旧格式简洁。
	o["id"]      = s.id;
	o["label"]   = s.label;
	o["classId"] = s.classId;
	if (!s.sourceToolId.isEmpty())
		o["sourceToolId"] = s.sourceToolId;
	switch (s.type)
	{
	case Shape_Rect:
		o["type"] = kTypeRect;
		o["cx"] = s.cx; o["cy"] = s.cy; o["w"] = s.w; o["h"] = s.h;
		break;
	case Shape_RotateRect:
		o["type"] = kTypeRotatedRect;
		o["cx"] = s.cx; o["cy"] = s.cy;
		o["w"] = s.w;   o["h"] = s.h;
		o["angle"] = s.angle;
		break;
	case Shape_Circle:
		o["type"] = kTypeCircle;
		o["cx"] = s.cx; o["cy"] = s.cy; o["r"] = s.r;
		break;
	case Shape_Ellipse:
		o["type"] = kTypeEllipse;
		o["cx"] = s.cx; o["cy"] = s.cy;
		o["r1"] = s.r; o["r2"] = s.r2;
		o["angle"] = s.angle;
		break;
	case Shape_Ring:
		o["type"] = kTypeRing;
		o["cx"] = s.cx; o["cy"] = s.cy; o["r1"] = s.r; o["r2"] = s.r2;
		break;
	case Shape_Arc:
		o["type"] = kTypeArc;
		o["cx"] = s.cx; o["cy"] = s.cy;
		o["rOuter"] = s.r; o["rInner"] = s.r2;
		o["startAngle"] = s.startAngle;
		o["endAngle"] = s.endAngle;
		o["span"] = s.span;
		break;
	case Shape_Polygon:
	{
		o["type"] = kTypePolygon;
		QJsonArray pts;
		for (const QPointF& p : s.pts)
		{
			QJsonArray pt;
			pt.append(p.x());
			pt.append(p.y());
			pts.append(pt);
		}
		o["points"] = pts;
		break;
	}
	default:
		break;
	}
	return o;
}

bool RecipeIO::shapeFromJson(const QJsonObject& o, DrawShapeItem& out)
{
	const QString type = o["type"].toString();
	DrawShapeType t;
	if      (type == kTypeRect)        t = Shape_Rect;
	else if (type == kTypeRotatedRect) t = Shape_RotateRect;
	else if (type == kTypeCircle)      t = Shape_Circle;
	else if (type == kTypeEllipse)     t = Shape_Ellipse;
	else if (type == kTypeRing)        t = Shape_Ring;
	else if (type == kTypeArc)         t = Shape_Arc;
	else if (type == kTypePolygon)     t = Shape_Polygon;
	else return false;

	DrawShapeItem s(t);
	// 业务元信息：统一恢复（旧文件缺字段时用默认值兜底）
	s.id      = o["id"].toString();
	s.label   = o["label"].toString();
	s.classId = o["classId"].toInt(-1);
	s.sourceToolId = o["sourceToolId"].toString();
	switch (t)
	{
	case Shape_Rect:
		s.cx = o["cx"].toDouble(); s.cy = o["cy"].toDouble();
		s.w  = o["w"].toDouble();  s.h  = o["h"].toDouble();
		break;
	case Shape_RotateRect:
		s.cx = o["cx"].toDouble(); s.cy = o["cy"].toDouble();
		s.w  = o["w"].toDouble();  s.h  = o["h"].toDouble();
		s.angle = o["angle"].toDouble();
		break;
	case Shape_Circle:
		s.cx = o["cx"].toDouble(); s.cy = o["cy"].toDouble(); s.r = o["r"].toDouble();
		break;
	case Shape_Ellipse:
		s.cx = o["cx"].toDouble(); s.cy = o["cy"].toDouble();
		s.r  = o["r1"].toDouble(); s.r2 = o["r2"].toDouble();
		s.angle = o["angle"].toDouble();
		break;
	case Shape_Ring:
		s.cx = o["cx"].toDouble(); s.cy = o["cy"].toDouble();
		s.r  = o["r1"].toDouble(); s.r2 = o["r2"].toDouble();
		break;
	case Shape_Arc:
		s.cx = o["cx"].toDouble(); s.cy = o["cy"].toDouble();
		s.r  = o["rOuter"].toDouble(); s.r2 = o["rInner"].toDouble();
		s.startAngle = o["startAngle"].toDouble();
		s.endAngle   = o["endAngle"].toDouble();
		s.span       = o["span"].toDouble();
		break;
	case Shape_Polygon:
	{
		const QJsonArray pts = o["points"].toArray();
		for (const auto& v : pts)
		{
			const QJsonArray pt = v.toArray();
			if (pt.size() >= 2)
				s.pts.append(QPointF(pt[0].toDouble(), pt[1].toDouble()));
		}
		break;
	}
	default:
		break;
	}
	out = std::move(s);
	return true;
}

QJsonObject RecipeIO::itemToJson(const InspectionItem& item)
{
	QJsonObject o;
	o["id"]            = item.id;
	o["name"]          = item.name;
	o["algorithmType"] = item.algorithmType;

	// RoiRef 序列化：{ roiId, followFrom(可选，空不写) }，含"跟随"持久化。
	QJsonArray rois;
	for (const RoiRef& r : item.rois)
	{
		QJsonObject ro;
		ro["roiId"] = r.roiId;
		if (!r.followFrom.isEmpty())
			ro["followFrom"] = r.followFrom;
		rois.append(ro);
	}
	o["rois"] = rois;

	o["params"]   = item.params;
	o["passRule"] = item.passRule;
	return o;
}

InspectionItem RecipeIO::itemFromJson(const QJsonObject& o)
{
	InspectionItem item;
	item.id            = o["id"].toString();
	item.name          = o["name"].toString();
	item.algorithmType = o["algorithmType"].toString();

	// RoiRef 反序列化。
	const QJsonArray rois = o["rois"].toArray();
	for (const auto& v : rois)
	{
		const QJsonObject ro = v.toObject();
		RoiRef ref;
		ref.roiId      = ro["roiId"].toString();
		ref.followFrom = ro["followFrom"].toString();
		item.rois.append(ref);
	}

	item.params   = o["params"].toObject();
	item.passRule = o["passRule"].toObject();
	return item;
}
