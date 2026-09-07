/*
 * 文件名：ResultRenderer.h
 * 职责：算法结果【渲染层】—— 把 AlgorithmResult 只读视图绘制到 QGraphicsScene
 *
 * 定位（严格数据/渲染分离）：
 *   - 数据层：AlgorithmResult / DetectionResultModel 只存结果，【零渲染依赖】；
 *   - 渲染层：本类【只读消费】结果数据，画到场景，【不写回数据】；
 *   - 单向依赖 数据 → 渲染（Render 读 Data，Data 不认识 Render）。
 *
 * 分派：按 AlgorithmResult 的四种结果载体分别画法：
 *   detections → DetectionBox  画"框 + 标签"
 *   poses      → PoseResult    画"中心十字 + 方向线 + 标签"
 *   geometry   → ResultGeom    按 GeomPrimitive 类型画"线段/圆/散点..."
 *   masks      → SegmentationMask 画"闭合轮廓"（未来 UNet）
 *
 * 生命周期契约（避免悬垂/泄漏）：
 *   本类持有 m_items（已画图元列表），clear() 负责 removeItem + delete 成对；
 *   场景的所有权归外部（ImageCanvasView::m_scene），本类不 delete 场景。
 */
#pragma once

#include <QList>
#include "AlgorithmResult.h"

class QGraphicsScene;
class QGraphicsItem;

class ResultRenderer
{
public:
	/*  构造：绑定目标场景（场景所有权仍在外部） */
	explicit ResultRenderer(QGraphicsScene* scene);

	/*  渲染一组结果（先 clear 旧叠层，再按类型分派绘制） */
	void render(const QList<AlgorithmResult>& results);

	/*  清除全部已画图元（removeItem + delete 成对） */
	void clear();

private:
	/*  各载体分派绘制（内部实现） */
	void drawDetection(const DetectionBox& box);
	void drawPose(const PoseResult& pose);
	void drawGeometry(const ResultGeom& geom);

	QGraphicsScene* m_scene = nullptr;      /*  目标场景（外部持有） */
	QList<QGraphicsItem*> m_items;          /*  已画图元（随 clear 销毁） */
};
