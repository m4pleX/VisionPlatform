/*
 *  文件名：ImageSource.h
 *  职责：图像源顶层数据结构（机器视觉平台的"数据流源头"一等公民）
 *
 *  定位（对齐 HALCON Image Acquisition / Cognex Camera Acquire Block 的共识）：
 *    - 图像源 = 数据流图（DAG）的【起点节点】，唯一输出端口类型是【图像】；
 *    - 它回答"这张图从哪来、属于谁"，是一个【有身份、可多实例、可反复取图】的源，
 *      而不是一张孤立的裸图。
 *
 *  ────────────────────────────────────────────────────────────────
 *  分层设计（三层，业界共识 + 业务代入测试收敛）：
 *
 *    ① ImageSource     源的身份/配置 —— 持久，多次运行不变（相机/文件/路径/参数/标定）
 *    ② AcquireSession  采集会话运行态 —— 单次"打开源"期间的连接状态/错误/重连，
 *                        介于"持久配置"与"逐帧数据"之间（历史缺失的一层）
 *    ③ ImageData       本次取到的图 —— 每次采集/取图产生（零拷贝引用 + 帧元信息）
 *
 *  ────────────────────────────────────────────────────────────────
 *  源类型收敛原则（2026-09 查证结论，防过度设计）：
 *    - 仅保留【File】与【Camera】两类一等源（+ Unknown 占位）；
 *    - 目录批量 / .seq 图像序列 本质是"File 的批量/有序形态"，不进 enum 的一等源语义，
 *      由 FileBatchMode 表达（对齐 HALCON：.seq 走 'File' 接口，非独立接口）；
 *    - 网络流 / RTSP 属安防监控协议，工业视觉走 GigE/U3V/CameraLink（本质是 Camera），
 *      【不】设为独立源类型。
 *
 *  ────────────────────────────────────────────────────────────────
 *  【流通票决策】（2026-09 查证业界三标杆 HALCON / VisionPro / VisionMaster 定论）：
 *    - 大厂【全部自建图像类型】（HImage / CogImage8Grey / CmvdImage），无一直接用
 *      OpenCV Mat 或 Qt QImage 当主表示 —— 为的是【表达力 + 隔离底层 + 承载语义】。
 *    - 本项目对齐：**ImageData 是流通票（canonical），cv::Mat 只是它的存储后端**。
 *        工具层统一消费 ImageData / shared_ptr<const cv::Mat>，【禁止】直接依赖裸 cv::Mat 值；
 *        QImage 仅是【渲染上屏】那一刻的显示桥接（零拷贝 + 显示时才 BGR→RGB）。
 *    - 好处：将来接 HALCON / YOLO / UNet，只需在 ImageData 内部做边界转换，
 *        工具层零改动；灰度图下 Mat↔HImage 接近零拷贝。
 * ======================================================================== */

#pragma once

#include <QString>
#include <QSize>
#include <QJsonObject>
#include <QList>
#include <QtGlobal>
#include <memory>
#include <optional>
#include <opencv2/core.hpp>

/* ========================================================================
 *  源类型枚举（一等源，仅两类 + 占位）
 * ======================================================================== */
enum class ImageSourceType
{
	File,      /*  磁盘图像（单张 / 目录批量 / .seq 序列，批量形态见 FileBatchMode） */
	Camera,    /*  物理相机（GigE / USB3 / Camera Link，对齐 open_framegrabber 硬件接口） */
	Unknown    /*  未指定（占位，等价于旧版"无来源"） */
};

/* ========================================================================
 *  文件源的批量形态（仅 type == File 有效；见"源类型收敛原则"）
 * ======================================================================== */
enum class FileBatchMode
{
	Single,      /*  单张图像 */
	Directory,   /*  目录批量：读取盘内所有/指定后缀图像（跑批/离线检测） */
	Sequence     /*  .seq 序列：文本列出的有序图像列表（离线复现现场连续帧） */
};

/* ========================================================================
 *  CameraConfig：相机源的专有配置（仅 type == Camera 有效）
 *
 *  抽象动机（对齐 HALCON open_framegrabber / Cognex 相机设备参数面板）：
 *    相机源的字段与 File 源【完全不同】，不能靠 path 字符串 + params 通用 JSON 压扁；
 *    跨厂商通项（型号/序列号/触发/曝光/增益/分辨率/帧率）→ 强类型字段；
 *    厂商特有项 → extra 兜底（防过早绑定某家 SDK）。
 *
 *  留坑位原则：此为【数据结构分支】占位，字段填默认值，不接采集逻辑；
 *  真正的 open/grab/连 SDK 属采集层职责，与本结构无关。
 * ======================================================================== */
struct CameraConfig
{
	/*  ---- 厂商与设备标识 ---- */
	QString vendor;        /*  厂商："HIKVISION" / "Basler" / "Daheng" / ... */
	QString model;         /*  型号（如海康 MV-CA050-10GM） */
	QString serialNo;      /*  序列号（设备唯一标识，替代旧 path 塞序列号的约定） */
	QString ip;            /*  GigE 相机 IP（USB3/CameraLink 留空） */

	/*  ---- 采集核心参数（跨厂商通项，强类型） ---- */
	/*  ⚠ 语义契约：数值字段【0 = 使用相机硬件默认值】。
	 *    序列化反序列化注意：JSON 里缺失字段 → 加载出来是 0 → 语义合法（等于"未设置"）。
	 *    【陷阱】不要把 0 当作"用户已修改的参数"：UI 面板必须区分"用户是否手动改过"，
	 *    不能拿 0 判断"改没改"。若将来要做"区分未修改 vs 真正设置 0"，需引入
	 *    std::optional<double>；现阶段【先不引入 optional 增加复杂度】，靠本契约约定。 */
	QString triggerMode;         /*  "continuous"（连续）/ "software"（软触发）/ "hardware"（硬触发） */
	double  exposureUs = 0;      /*  曝光时间（微秒；0 = 相机默认/自动） */
	double  gain       = 0;      /*  增益（0 = 默认/自动） */
	int     width      = 0;      /*  采集分辨率宽（0 = 相机默认） */
	int     height     = 0;      /*  采集分辨率高（0 = 相机默认） */
	double  frameRate  = 0;      /*  帧率（0 = 相机默认） */
	QString pixelFormat;         /*  像素格式："Mono8" / "BayerRG8" / ...（空 = 默认） */

	/*  ---- 厂商特有配置兜底 ---- */
	QJsonObject extra;    /*  海康/Basler 等的私有属性（通用容器，Rule of Three 后强类型化） */
};

/* ========================================================================
 *  采集会话状态（会话级运行态）
 *
 *  描述"打开一个 ImageSource 之后、本次采集会话"的健康状况。
 *  与 ImageSource（持久配置）不同：会话有生命周期（连接→采集→断开→重连）；
 *  与 FrameMeta（每帧）不同：会话状态跨帧共享，不逐帧重复。
 *
 *  ⚠⚠ 序列化契约（务必遵守）：
 *    AcquireSession 是【运行态】，生命周期随程序运行，一旦进程退出即失效。
 *    【绝对禁止】将 AcquireSession 序列化写入工程 JSON / 持久化保存；
 *    持久化的只有 ImageSource（配置层），会话状态每次运行时【重新建立】。
 * ======================================================================== */
enum class AcquireState
{
	Idle,         /*  未打开 */
	Opening,      /*  打开/连接中 */
	Ready,        /*  就绪：可采集 */
	Acquiring,    /*  采集中 */
	Disconnected, /*  断连（相机掉线/文件被移动） */
	Error         /*  出错（错误码见 AcquireSession.lastError） */
};

struct AcquireSession
{
	/*  ---- 状态机 ---- */
	AcquireState state = AcquireState::Idle;

	/*  ---- 诊断信息（断连/错误时有效） ---- */
	int     lastError  = 0;   /*  错误码（0 = 无错误；具体码值由采集层定义） */
	int     retryCount = 0;   /*  本次会话累计重连次数（诊断/告警阈值） */
	QString lastMessage;      /*  人类可读的诊断消息 */

	/*  ---- 文件批量游标（仅 File + 非 Single 模式有效） ---- */
	qint64  cursor     = 0;   /*  当前读到第几帧/第几张（0 起） */
	qint64  totalCount = 0;   /*  总数（-1 = 未知，用于进度/回显） */

	bool isReady() const { return state == AcquireState::Ready || state == AcquireState::Acquiring; }
};

/* ========================================================================
 *  ImageSource：源的身份 / 配置（持久层）
 *
 *  描述"图像从哪来"，不含本次采集到的像素数据。多次运行不变。
 * ======================================================================== */
struct ImageSource
{
	/*  ---- 身份 ---- */
	QString id;           /*  唯一标识（空 = 未指定，由上层生成）；
	                       用于下游节点显式引用："我吃 imageSource[id] 的图"。
	                       对齐 DrawShapeItem::id / InspectionItem::id 的一致性 */
	QString name;         /*  UI 展示名（用户可读，如 "相机1" / "模板图"） */

	/*  ---- 来源类型与出处 ---- */
	ImageSourceType type = ImageSourceType::Unknown;

	/*  File 源：文件路径 / 目录 / .seq 文件路径（按 batch 模式解释） */
	QString path;
	/*  File 源的批量形态（type==File 有效；默认单张） */
	FileBatchMode batch = FileBatchMode::Single;
	/*  Directory 模式的文件过滤（空 = 全部图像）。
	 *  约定：仅支持【glob 通配符】，不写正则；多模式用分号分隔，如 "*.png;*.jpg;*.bmp"。
	 *  ⚠ 业务层解析需自行按 ';' 拆分逐个 glob 匹配（此处只存字符串，不做拆分）。 */
	QString filePattern;

	/*  Camera 源专有配置（type==Camera 有效） */
	CameraConfig camera;

	/*  ---- 标定挂靠（相机源的物理↔图像坐标映射，占位，未落地） ---- */
	QJsonObject calibration;   /*  标定矩阵/畸变参数（通用容器，Rule of Three 后强类型化） */

	/*  ---- 采集相关配置（预留；通用容器，后强类型化） ---- */
	QJsonObject params;

	/*  配置完整性【浅校验】：type 已指定，且出处已填（纯字段自洽，无 I/O）。
	 *  ⚠ 语义边界（对齐 C++ Core Guidelines P.7 / struct 无深层不变量）：
	 *    "文件是否存在 / 相机是否可达" 属运行时可用性，由【采集层 acquire()】判定，
	 *    本纯数据结构【不越权】代做。 */
	bool hasConfig() const
	{
		if (type == ImageSourceType::Unknown) return false;
		if (type == ImageSourceType::Camera)
			return !camera.serialNo.isEmpty() || !camera.ip.isEmpty();
		return !path.isEmpty();
	}
};

/* ========================================================================
 *  FrameMeta：采集帧元信息（每次采集产生，与像素数据并列，非"源配置"）
 *
 *  描述"这一帧是怎么采来的"（时间/序号/曝光等运行时上下文），
 *  与像素数据是两种语义，故独立成结构。
 *  对齐：OpenCV CAP_PROP_POS_FRAMES / 时间戳；HALCON capture 参数；
 *        Cognex Acquire Block 的帧时间戳。
 * ======================================================================== */
struct FrameMeta
{
	/*  ⚠ 时钟约定：建议用【单调时钟 QElapsedTimer】，勿用系统 wall-time ——
	 *   系统时间被手动修改（NTP 校准/改时区）会导致时间戳【乱跳】，破坏帧序/延迟诊断。 */
	qint64 timestamp  = 0;   /*  采集时刻（ms；单调时钟 QElapsedTimer 的 elapsed，非 wall-time） */
	qint64 frameIndex = 0;   /*  帧序号（连续递增，用于丢帧/乱序诊断） */
	QJsonObject extra;       /*  扩展：曝光/增益/触发源/设备状态等（通用容器，后强类型化） */
};

/* ========================================================================
 *  ImageData：本次取到的图（每次采集/取图产生）
 *
 *  图像像素零拷贝只读引用（shared_ptr<const cv::Mat>），对齐 ToolContext.image。
 *
 *  ⚠ 单一数据源原则：宽高【不从额外字段缓存】，一律派生自 image（cols/rows）。
 *
 *  sourceId【来源快照，非冗余】：
 *    ImageData 是值语义、会被拷贝/传递（入 ImageBuffer、作函数实参、向下游流转），
 *    一旦脱离 QHash<id, ImageData> 容器，若无 sourceId 就会丢失"来自哪个源"。
 *    故 sourceId 必须随帧走（尤其多相机混流时靠它区分帧归属）。
 *    它与容器 key 的一致性由插入方保证（插入时填/校验），不是删除它的理由。
 * ======================================================================== */
struct ImageData
{
	std::shared_ptr<const cv::Mat> image;   /*  图像数据（只读引用贯穿，零拷贝） */

	/*  来源 id 快照（对应 ImageSource.id）：脱离容器仍可追溯；空 = 未关联源 */
	QString sourceId;

	/*  采集帧元信息（采集层填充；文件/目录源可留空） */
	FrameMeta meta;

	/*  有效性：图像非空 */
	bool isValid() const { return image && !image->empty(); }

	/*  ---- 宽高/尺寸：全部派生自 image（单一数据源，无缓存冗余） ---- */
	int    width()  const { return image ? image->cols : 0; }
	int    height() const { return image ? image->rows : 0; }
	QSize  size()   const { return image ? QSize(image->cols, image->rows) : QSize(); }

	/*  从图像构造（等价一次"取图结果")；sourceId 空则调用方后续填/校验 */
	static ImageData fromMat(const std::shared_ptr<const cv::Mat>& m, const QString& srcId = QString())
	{
		ImageData d;
		d.image = m;
		d.sourceId = srcId;
		return d;
	}
};

/* ========================================================================
 *  ImageBuffer：单源多帧序列（异步采集/连拍的帧组织容器）
 *
 *  一个 ImageSource 连续采集产生的多帧（硬件触发连拍 / 异步采集中帧队列），
 *  按采集顺序组织；帧间时序由 FrameMeta.frameIndex / timestamp 表达。
 * ======================================================================== */
struct ImageBuffer
{
	QList<ImageData> frames;   /*  帧序列（采集顺序；空 = 无待处理帧） */

	void append(const ImageData& frame) { frames.append(frame); }

	int  size() const { return frames.size(); }
	bool isEmpty() const { return frames.isEmpty(); }

	/*  取最新一帧（值拷贝，不弹出）。
	 *  ⚠ 内存安全：返回 std::optional<ImageData>【值拷贝】，而非裸指针——
	 *    避免 frames append/clear 后裸指针悬空。拷贝仅 shared_ptr 引用计数+1，
	 *    像素【不复制】（零拷贝性质保留）。空返回 nullopt。 */
	std::optional<ImageData> latest() const
	{
		if (frames.isEmpty()) return std::nullopt;
		return frames.constLast();
	}
};
