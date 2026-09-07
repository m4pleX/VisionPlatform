/*
 * 文件名：ITool.h
 * 职责：视觉工具的统一抽象接口（最终契约，2026-09 定稿）
 *
 * 设计决策（经真实场景模拟 + 业界查证 + 安全性审视三重验证）：
 *   - 【薄接口】：只放所有工具都通用的最小公分母方法，
 *     不预埋视觉检测特有形状，为未来「大类分层」「百级子类」留后路；
 *   - 【输入输出分离】：run() 输入是 const 只读 ToolContext，输出靠 ToolResult 返回值，
 *     工具编译期就无法篡改输入（C++ Core Guidelines const 正确性）；
 *   - 【值语义 + 智能指针】：小对象（几何/结果）值传递，仅图像用 shared_ptr<const cv::Mat>
 *     引用贯穿（零拷贝 + 只读），对齐 C++ Core Guidelines P.10/F.16/I.11；
 *   - 禁止 std::function 按值返回非平凡类型（曾致 0xC0000005 崩溃），
 *     统一用虚接口 + 值语义。
 */
#pragma once

#include <QString>
#include <QList>
#include <QHash>
#include <QJsonObject>
#include <memory>
#include <opencv2/core.hpp>

#include "AlgorithmResult.h"
#include "DrawShapeData.h"
#include "ImageSource.h"
#include "ToolDefinition.h"

/* ========================================================================
 *  工具执行上下文（输入侧，只读）
 * ======================================================================== */
struct ToolContext
{
	/*  ===== 图像源（演进为多实例一等公民） ===== */
	/*  imageSources：本次运行的全部图像源（含身份 id），可多相机/多图并存。
	 *  工具据此声明"我消费 imageSources[id] 的图像"，取代旧版隐式"取第一张"。
	 *  数据零拷贝只读贯穿（shared_ptr<const cv::Mat>）。 */
	QHash<QString, ImageData> imageSources;

	/*  image：兼容字段，等价 imageSources[""]（无 id 单图场景），
	 *  与现有工具 ctx.image 调用无缝衔接；多图像源优先用 imageSources。
	 *  【迁移标记】2016-09：保留以平滑过渡，工具逐个迁移到 imageSources 后废弃。 */
	std::shared_ptr<const cv::Mat> image;

	/*  ===== 用户输入几何 ===== */
	/*  shapes：值语义（存值不存指针，const 才能层层防篡改）。
	 *  【迁移标记】同样存在"哪个工具用哪个 ROI"的显式绑定缺口，
	 *  未来经 InspectionItem.roiIds 落地（见 InspectionItem.h）。 */
	QList<DrawShapeItem> shapes;

	/*  ===== 上游结果 ===== */
	/*  值语义，按 toolId 作 Key 取出（下游据 name 精准定位上游产出） */
	QHash<QString, AlgorithmResult> results;

	/*  未来扩展点（不影响接口签名，需求落地时追加）：
	 *    - acquireSessions: QHash<QString, AcquireSession> 采集会话运行态（断连/错误/重连）
	 *    - buffers:         QHash<QString, ImageBuffer>    单源多帧缓冲（异步采集/连拍）
	 *    - 标定矩阵已在 ImageSource.calibration 占位，无需在此重复。
	 *  结构已定义于 ImageSource.h，待采集层真实落地后再接入本上下文。 */
};

/* ========================================================================
 *  工具执行结果（输出侧，独立返回）
 * ======================================================================== */
struct ToolResult
{
	AlgorithmResult data;      /*  结果数据（值语义，隐式共享） */
	bool     ok    = true;     /*  Pass / Fail */
	QString error;             /*  失败原因（ok=false 时有效） */
};

/* ========================================================================
 *  工具接口（薄、通用、可扩展）
 * ======================================================================== */
class ITool
{
public:
	virtual ~ITool() = default;

	/*  算法唯一标识（如 "grayDefect"） */
	virtual QString name() const = 0;

	/*  任务大类（采集/定位/检测/测量/识别/输出） */
	virtual ToolCategory category() const = 0;

	/*  执行：输入 const 只读，输出独立返回（输入输出分离，防篡改） */
	virtual ToolResult run(const ToolContext& ctx) = 0;

	/*  参数：独立于 run，序列化边界才做强类型转换 */
	virtual void loadParams(const QJsonObject& params) = 0;
	virtual QJsonObject saveParams() const = 0;
};
