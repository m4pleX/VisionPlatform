/*
 *  文件名：InspectionItem.h
 *  职责：检测项数据模型（ROI 与算法的关联层）
 *
 *  定位：
 *    - 检测项 = 算法类型 + 引用的 ROI 集合（含用法）+ 参数 + 判定规则；
 *    - 是"算法用哪个 ROI 实例、怎么用"的答案：
 *        每个 RoiRef = { roiId(指向哪个) + followFrom(怎么用：固定/跟随) }；
 *    - 值语义（与 DrawShapeItem 的指针语义区分），供 RecipeIO 序列化。
 *
 *  ────────────────────────────────────────────────────────────────
 *  【设计修订记录】（2026-09，修复早期 roiIds:QStringList 的扁平引用漏洞）：
 *    早期 roiIds 仅存纯 id 列表，未表达"每个 ROI 的使用方式"，
 *    导致"跟随(applyPose 校正) vs 固定"这个维度无处安放。
 *    现升级为 RoiRef：引用 = id + 用法。未来若再出现其它用法
 *    （如屏蔽 mask / 测量基准），在 RoiRef 上扩展，不再改此处结构。
 * ======================================================================== */

#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>

/* ========================================================================
 *  RoiRef：检测项对某个 ROI 的【引用 + 用法】
 *
 *  roiId      绑定的 ROI id（对应 DrawShapeItem::id）
 *  followFrom 跟随来源（仿射变换/校正）：
 *               - 空 = 固定：直接使用人工绘制的基准 ROI，不做校正；
 *               - 非空 = 继承：运行时取该【定位工具 id】产出的 Pose2D，
 *                         对 roiId 指向的基准 ROI 执行 ShapeGeometry::applyPose，
 *                         生成"跟随工件的 ROI"再供本工具消费。
 *               （对齐海康 VM 的"ROI 创建：继承/矩形区域"、Cognex 的 ROI 引脚连定位输出）
 * ======================================================================== */
struct RoiRef
{
	QString roiId;        /*  绑定的 ROI id（对应 DrawShapeItem::id） */
	QString followFrom;   /*  跟随来源定位工具 id；空 = 固定不跟随 */

	/*  便捷：是否声明了"跟随定位" */
	bool isFollowing() const { return !followFrom.isEmpty(); }
};

/* ========================================================================
 *  InspectionItem：检测项
 * ======================================================================== */
struct InspectionItem
{
	QString      id;              /*  检测项唯一标识 */
	QString      name;            /*  检测项名称（UI 展示） */
	QString      algorithmType;   /*  算法类型（"grayDefect" / 未来 "measure" / "yolo" ...） */

	/*  ROI 引用列表：每个含 id + 用法（固定/跟随）。
	 *  空 = 不绑定任何 ROI（整图 / 默认行为）。 */
	QList<RoiRef> rois;

	QJsonObject  params;          /*  算法参数（通用容器，避免过早绑定具体参数结构体） */
	QJsonObject  passRule;        /*  判定规则（OK/NG，通用容器） */

	/*  便捷：所有绑定 ROI 的纯 id 列表（供工具快速按 id 匹配，兼容旧接口） */
	QStringList roiIdList() const
	{
		QStringList ids;
		for (const RoiRef& r : rois)
			ids << r.roiId;
		return ids;
	}
};
