/*
 *  文件名：RecipeIO.h
 *  职责：方案数据序列化/反序列化（内存几何模型 <-> JSON 文件）
 *
 *  依赖：DrawShapeData（纯几何数据模型），不依赖任何渲染类
 *  设计约束：
 *    本类只做数据层转换，不接触 QGraphicsItem、场景、UI；
 *    保证"方案文件 -> 几何数据 -> 重建图形"的闭环。
 *
 *  JSON schema 契约（稳定序列化协议，供算法层 import/export）：
 *    文件级 root: { "version": int(当前=1，旧文件可缺省按1处理), "imagePath", "imageSize": {width,height}, "shapes": [...] }
 *    形状级: { "type", "id", "label", "classId", "sourceToolId"(可选), 以及与 shape type 对应的几何字段键 }
 *    常见形状键: rect/rotatedRect/circle/ellipse/ring 用 cx,cy,w,h,r,r1,r2,angle；
 *                arc 用 cx,cy,rOuter,rInner,startAngle,endAngle,span；
 *                polygon 用 points(点数组)。
 *    检测项级（预留，供多算法/多 ROI 绑定）: { "id", "name", "algorithmType", "roiIds", "params", "passRule" }
 */
#pragma once

#include <QJsonObject>
#include <QList>
#include "DrawShapeData.h"
#include "InspectionItem.h"

class RecipeIO
{
public:
	/*  单个形状 -> JSON 对象（含 type 标识） */
	static QJsonObject shapeToJson(const DrawShapeItem& s);

	/*  JSON 对象 -> 形状（值语义，由调用方持有）；解析失败返回 false */
	static bool shapeFromJson(const QJsonObject& o, DrawShapeItem& out);

	/*  检测项 -> JSON 对象（预留：供多算法/多 ROI 绑定使用） */
	static QJsonObject itemToJson(const InspectionItem& item);

	/*  JSON 对象 -> 检测项 */
	static InspectionItem itemFromJson(const QJsonObject& o);
};
