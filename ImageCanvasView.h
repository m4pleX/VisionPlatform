/*
 *  文件名：ImageCanvasView.h
 *  职责：画布主控制器，交互中枢（胶水层），统筹全部子模块
 *
 *  架构总览：
 *    ImageCanvasView (主交互调度层)
 *    +-- DrawShapeItem          形状数据模型（输入几何：ROI / 基准线，不含渲染对象）
 *    +-- ShapePainter           形状绘制工具：创建QGraphicsItem、统一设置样式
 *    +-- ShapeHandleHelper      控制点管理器：控制点创建、更新、显隐控制
 *    +-- ParamPanelWidget       参数面板：动态生成UI、同步读写形状参数
 *    +-- ToolbarController      工具栏控制器：按钮状态、缩放信息联动
 *    +-- DetectionResultModel   算法结果宿主（只读结果的多实例管理）
 *    +-- GrayDefectDetector     算法引擎（OpenCV 灰度缺陷检测）
 *
 *  核心功能：
 *    - 画布场景管理、图像加载
 *    - 7类输入几何（ROI/基准）的交互式绘制流程
 *    - 几何编辑：拖拽控制点、旋转、平移、参数面板编辑
 *    - 算法结果只读上屏（独立叠层，不参与编辑）
 *
 *  交互事件流：
 *    eventFilter 捕获鼠标事件
 *      → 视图模式：画布平移、hover高亮
 *      → 绘制模式：各类图形预览绘制
 *    图形提交链路：
 *      commitXxx() → clearSceneShape() → rebuildShapeOnScene()
 *
 *  模块依赖：DrawShapeItem, ShapePainter, ShapeHandleHelper, ParamPanelWidget, ToolbarController
 *  设计约束：
 *    本类不实现具体绘制、控件渲染细节，全部委托子模块实现；
 *    全局交互状态统一集中在此维护。
 */
#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_ImageCanvasView.h"
#include "DrawShapeData.h"
#include "ImageSource.h"
#include "RecipeIO.h"
#include "ToolbarController.h"
#include "DetectionResultModel.h"
#include "ITool.h"

class QDockWidget;
class QTreeWidget;
class QGraphicsEllipseItem;
class QGraphicsItem;
class QGraphicsLineItem;
class QGraphicsPathItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsSimpleTextItem;
class QGraphicsScene;
class QKeyEvent;
class QPushButton;

class ParamPanelWidget;
struct ParamField;
class ShapePainter;
class ShapeHandleHelper;
struct ShapeHandleSet;

/*  主窗口：场景管理、绘制/编辑流程、事件分发 */
/*  胶水层，委托子模块处理具体逻辑 */
class ImageCanvasView : public QMainWindow
{
	Q_OBJECT

public:
	ImageCanvasView(QWidget* parent = nullptr);
	~ImageCanvasView() override;

protected:
	bool eventFilter(QObject* obj, QEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private slots:
	void slotZoomIn();
	void slotZoomOut();
	void slotZoomReset();
	void slotZoomFit();
	void slotToggleCenterCross();
	void slotToggleLineWidth();
	void slotToggleControlPoints();
	void slotLoadImage();
	void slotSaveRecipe();
	void slotLoadRecipe();
	void slot_draw_shape_changed(int index);
	void slotResetShape();
	void slotOpenParamPanel();
	void slotRunDetect();
	void slotRunLocate();
	void slotRunCaliper();
	void slotRunFlow();

private:
	/*  ====================== UI 绑定 ====================== */
	Ui::ImageCanvasViewClass ui;

	/*  ====================== 核心子模块 ====================== */
	ShapePainter*         m_painter       = nullptr;  /*  形状绘制 */
	ShapeHandleHelper*    m_handleHelper  = nullptr;  /*  控制点管理 */
	ToolbarController*    m_toolbar       = nullptr;  /*  工具栏状态 */
	ParamPanelWidget*     m_paramPanel    = nullptr;  /*  参数面板 */

	/*  ====================== 场景与图像 ====================== */
	QGraphicsScene*       m_scene         = nullptr;
	QGraphicsPixmapItem*  m_pixmapItem    = nullptr;
	QGraphicsRectItem*    m_bgItem        = nullptr;
	QGraphicsLineItem*    m_spotAbsorber  = nullptr;  /*  OpenGL 残影吸收线 */
	double                m_scaleValue    = 1.0;
	QString               m_imagePath;               /*  当前图像路径 */

	/*  ====================== 图像源（顶层身份，多实例底座） ====================== */
	/*  当前激活图像源的身份/配置（持久）；本次像素数据在运行经 ImageData 零拷贝传入 ctx。
	 *  未来多相机/多图场景下扩展为 QHash<id, ImageSource>。 */
	ImageSource           m_imageSource;

	bool m_showCenterCross = false;
	bool m_showControlPoints = true;
	QGraphicsLineItem* m_crossH = nullptr;
	QGraphicsLineItem* m_crossV = nullptr;

	/*  ====================== 形状数据 ====================== */
	/*  【值语义数据模型】m_shapes 持有 DrawShapeItem 值（非指针），所有权/生命周期由容器统一管理；
	 *   形状定位一律用【索引】（m_activeShapeIndex / m_dragShapeIndex，-1 = 无效），
	 *   需原地修改几何处用 m_shapes[idx] 取引用；彻底消除散落的 new/delete 与悬垂指针风险。 */
	QList<DrawShapeItem> m_shapes;          /*  值容器：多实例输入几何（ROI / 基准线） */
	DrawShapeType        m_currentShape     = Shape_Rect;
	int                  m_activeShapeIndex = -1;   /*  当前选中形状索引（-1 = 无选中） */

	/*  渲染句柄（与 m_activeShape 对应的 QGraphicsItem + handle set） */
	QGraphicsItem*  m_shapeItem       = nullptr;
	ShapeHandleSet* m_activeHandleSet = nullptr;

	/*  ====================== 绘制模式 UI ====================== */
	QWidget*     m_drawModeOverlay = nullptr;
	QPushButton* m_btnCancelDraw   = nullptr;
	QLabel*      m_infoLabel       = nullptr;

	/*  ====================== 样式状态 ====================== */
	QColor m_colorSelected   = QColor(0, 120, 255);
	double m_penWidth        = 2.0;
	bool   m_isShapeHovered  = false;
	bool   m_thinLine        = false;

	/*  ====================== 检测结果（宿主 + 只读叠层） ====================== */
	/*  【生命周期契约】
	 *   - m_detectModel：值语义，销毁 = clear() 或随本类析构自动释放，禁止手动 delete 其内部数据。
	 *   - m_detectResultItems / m_detectResultLabels：裸指针，所有权归 m_scene；
	 *     销毁唯一入口 = clearDetectResultOverlay()（removeItem + delete 成对），
	 *     禁止在其它位置裸 delete。 */
	DetectionResultModel m_detectModel;                 /*  结果宿主：多结果/多实例的数据层 */
	QList<QGraphicsRectItem*> m_detectResultItems;      /*  叠层：结果框 */
	QList<QGraphicsSimpleTextItem*> m_detectResultLabels; /*  叠层：结果标签 */

	/*  ====================== 绘制模式临时状态 ====================== */
	enum InteractionMode { Mode_View, Mode_Draw, Mode_None };
	InteractionMode m_mode     = Mode_None;
	int             m_drawStep = 0;
	QPointF         m_anchorPoint;

	/*  ghost 预览项 */
	QGraphicsRectItem*    m_ghostRect       = nullptr;
	QGraphicsEllipseItem* m_ghostEllipse    = nullptr;
	QGraphicsEllipseItem* m_ghostEllipse2   = nullptr;
	QGraphicsPathItem*    m_ghostArcPath    = nullptr;
	QGraphicsRectItem*    m_ghostCircumRect = nullptr;

	/*  圆形三点绘制 */
	QPointF m_circlePt1, m_circlePt2;
	QGraphicsEllipseItem* m_circleMarker1 = nullptr;
	QGraphicsEllipseItem* m_circleMarker2 = nullptr;

	/*  Arc 绘制 */
	double m_arcStartAngle = 0;
	double m_arcEndAngle   = 0;

	/*  多边形绘制 */
	QList<QPointF>               m_tempPolyPts;
	QList<QGraphicsEllipseItem*> m_circleMarkers;
	QGraphicsLineItem*           m_ghostPolyLine = nullptr;

	/*  ====================== 拖拽临时状态 ====================== */
	bool m_isDraggingHandle = false;
	bool m_isRotating       = false;
	bool m_isDraggingShape  = false;

	int                 m_dragShapeIndex  = -1;   /*  正在拖拽的形状索引（-1 = 无） */
	int                 m_dragHandleIndex = -1;
	double              m_dragStartAngle  = 0;
	ShapeDragRect       m_dragStartRect;
	QPointF             m_dragOffset;
	QPointF             m_dragStartCenter;

	/*  ====================== 方法 ====================== */
	void clearSceneShape();
	void clearAllShapes();
	void rebuildShapeOnScene(int shapeIndex);
	bool loadImageFromPath(const QString& path);

	void showDrawModeOverlay();
	void hideDrawModeOverlay();
	void updateScaleUI();
	void updateCenterCross();
	void updateInfoLabel(const QPointF& scenePos, const QPoint& globalPos);

	void showParamPanel();
	void hideParamPanel();
	void applyParamAndRedraw();
	void syncParamPanel(int shapeIndex);

	void startDraw(DrawShapeType type);
	void stopDraw();
	void updateGhostRect(const QPointF& scenePos);
	void commitShapeGeneric(DrawShapeType type);
	void commitRect();
	void commitRotatedRect();
	void commitCircle();
	void commitEllipse();
	void commitRing();
	void commitArc();
	void commitPolygon();

	/*  按类型查找形状索引（单实例模型：返回首个匹配，未找到返回 -1） */
	int findShapeByType(DrawShapeType type);

	QGraphicsEllipseItem* handleAt(const QPointF& scenePos) const;

	/*  拖拽编辑后统一刷新控制点位置与参数面板（等价于原 update*FromHandle 末尾副作用） */
	void refreshActiveShape();

	void applyShapeHover(bool hover);

	/*  清除检测结果叠层（框 + 标签图形项） */
	void clearDetectResultOverlay();
	/*  由 m_detectModel 驱动渲染检测叠层 */
	void renderDetectOverlay();

	/*  定位结果叠层（临时验证用，独立于检测叠层） */
	QList<QGraphicsItem*> m_locateResultItems;
	/*  清除定位结果叠层 */
	void clearLocateResultOverlay();

	/*  卡尺结果叠层（临时验证用） */
	QList<QGraphicsItem*> m_caliperResultItems;
	/*  清除卡尺结果叠层 */
	void clearCaliperResultOverlay();

	/*  ====================== 流程编排（流程树 Dock） ====================== */
	QDockWidget* m_flowDock   = nullptr;   /*  流程树停靠面板 */
	QTreeWidget* m_flowTree   = nullptr;   /*  流程树（可拖拽排序） */
	QPushButton* m_btnRunFlow = nullptr;   /*  运行流程按钮 */

	/*  初始化流程树 Dock（构造末尾调用） */
	void setupFlowDock();
	/*  从流程树读取当前顺序，构造 ToolStep 列表 */
	QList<ToolStep> collectSteps() const;
};
