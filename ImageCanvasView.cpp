#include "ImageCanvasView.h"
#include "ShapePainter.h"
#include "ShapeHandleHelper.h"
#include "ParamPanelWidget.h"
#include "ParamFieldFactory.h"
#include "ScaleConfig.h"
#include "ShapeEditor.h"
#include "ShapeGeometry.h"
#include "CvImageConverter.h"
#include "GrayDefectDetector.h"
#include "PoseLocator.h"
#include "CaliperDetector.h"
#include "VisionTools.h"

#include <opencv2/core.hpp>
#include <memory>

#include <QtAlgorithms>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QDoubleSpinBox>
#include <QOpenGLWidget>
#include <QScrollArea>
#include <QDockWidget>
#include <QTreeWidget>
#include <QDebug>

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

// ===== 构造/析构 =====

ImageCanvasView::ImageCanvasView(QWidget* parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);

	m_scene = new QGraphicsScene(this);
	m_painter = new ShapePainter(m_scene);
	m_handleHelper = new ShapeHandleHelper(m_scene);
	m_activeHandleSet = new ShapeHandleSet();
	m_toolbar = new ToolbarController(ui.tool_btn_toggle_cross, ui.tool_btn_toggle_control_points,
		ui.tool_btn_toggle_line_width, ui.info_lab_scale_ratio, ui.info_lab_resolution, this);
	ui.canvas_view_main->setScene(m_scene);
	ui.canvas_view_main->setViewport(new QOpenGLWidget());
	ui.canvas_view_main->setMouseTracking(true);
	ui.canvas_view_main->setDragMode(QGraphicsView::NoDrag);

	m_pixmapItem = new QGraphicsPixmapItem();
	m_scene->addItem(m_pixmapItem);

	m_bgItem = new QGraphicsRectItem;
	m_bgItem->setZValue(-1);
	m_bgItem->setPen(Qt::NoPen);

	QImage texImg(8, 8, QImage::Format_RGB32);
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
			texImg.setPixel(x, y, ((x / 4) + (y / 4)) % 2 == 0 ? qRgb(100, 100, 100) : qRgb(60, 60, 60));

	QBrush checkerBrush;
	checkerBrush.setStyle(Qt::TexturePattern);
	checkerBrush.setTextureImage(texImg);
	m_bgItem->setBrush(checkerBrush);

	const qreal INF = 1000000;
	m_bgItem->setRect(-INF, -INF, INF * 2, INF * 2);
	m_scene->addItem(m_bgItem);

	m_crossH = new QGraphicsLineItem();
	m_crossV = new QGraphicsLineItem();
	QPen pen(QColor(0, 120, 255));
	pen.setWidth(1);
	pen.setCosmetic(true);
	m_crossH->setPen(pen);
	m_crossV->setPen(pen);
	m_crossH->setZValue(99);
	m_crossV->setZValue(99);
	m_crossH->setVisible(false);
	m_crossV->setVisible(false);
	m_scene->addItem(m_crossH);
	m_scene->addItem(m_crossV);

	QPixmap defaultPixmap(1280, 960);
	defaultPixmap.fill(Qt::white);
	m_pixmapItem->setPixmap(defaultPixmap);

	// OpenGL QPen 残影吸收线（z=999，0.01px透明，终身驻留）
	//m_spotAbsorber = m_scene->addLine(0, 0, 0, 0.01, QPen(QColor(0,0,0,1), 1));
	//m_spotAbsorber->setZValue(999);

	ui.canvas_view_main->viewport()->installEventFilter(this);
	ui.canvas_view_main->viewport()->setMouseTracking(true);

	connect(ui.tool_btn_zoom_in, &QPushButton::clicked, this, &ImageCanvasView::slotZoomIn);
	connect(ui.tool_btn_zoom_out, &QPushButton::clicked, this, &ImageCanvasView::slotZoomOut);
	connect(ui.tool_btn_zoom_reset, &QPushButton::clicked, this, &ImageCanvasView::slotZoomReset);
	connect(ui.tool_btn_zoom_fit, &QPushButton::clicked, this, &ImageCanvasView::slotZoomFit);
	connect(ui.tool_btn_toggle_cross, &QPushButton::clicked, this, &ImageCanvasView::slotToggleCenterCross);
	connect(ui.tool_btn_toggle_line_width, &QPushButton::clicked, this, &ImageCanvasView::slotToggleLineWidth);
	connect(ui.tool_btn_toggle_control_points, &QPushButton::clicked, this, &ImageCanvasView::slotToggleControlPoints);

	connect(ui.draw_btn_load_image, &QPushButton::clicked, this, &ImageCanvasView::slotLoadImage);
	connect(ui.draw_btn_save_recipe, &QPushButton::clicked, this, &ImageCanvasView::slotSaveRecipe);
	connect(ui.draw_btn_load_recipe, &QPushButton::clicked, this, &ImageCanvasView::slotLoadRecipe);
	connect(ui.draw_btn_run_detect, &QPushButton::clicked, this, &ImageCanvasView::slotRunDetect);
	connect(ui.draw_btn_run_locate, &QPushButton::clicked, this, &ImageCanvasView::slotRunLocate);
	connect(ui.draw_btn_run_caliper, &QPushButton::clicked, this, &ImageCanvasView::slotRunCaliper);
	connect(ui.draw_cbox_shape_type, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
			this, &ImageCanvasView::slot_draw_shape_changed);
	connect(ui.btn_reset_rect, &QPushButton::clicked, this, &ImageCanvasView::slotResetShape);
	connect(ui.draw_btn_open_param, &QPushButton::clicked, this, &ImageCanvasView::slotOpenParamPanel);

	ui.info_lab_scale_ratio->setText("100%");
	m_toolbar->updateResolution(1280, 960);

	m_infoLabel = new QLabel(this);
	m_infoLabel->setStyleSheet("background: white; color: black; padding: 2px 4px; border:1px solid #999;");
	m_infoLabel->setVisible(false);

	// 延迟执行 fit，确保窗口布局完成、viewport 尺寸已确定
	QTimer::singleShot(0, this, [this]() { slotZoomFit(); });

	// 流程编排：左侧流程树 Dock（最小侵入，不在 .ui 加东西）
	setupFlowDock();
}

ImageCanvasView::~ImageCanvasView() {
	delete m_painter;
	delete m_handleHelper;
	delete m_activeHandleSet;
	delete m_toolbar;
	// m_shapes 为值容器，随本类析构自动释放，无需手动 delete。
	// 显式清理检测结果：数据层清空 + 渲染层销毁（removeItem + delete），
	// 不依赖 m_scene 的隐式析构。此处 m_scene 仍存活（parent 为本类，子对象稍后才销毁）。
	m_detectModel.clear();
	clearDetectResultOverlay();
}

// ===== 事件处理 =====

bool ImageCanvasView::eventFilter(QObject* obj, QEvent* event)
{
	static QPoint lastViewPos;
	static bool isPanning = false;

	if (obj == m_drawModeOverlay)
	{
		if (event->type() == QEvent::MouseButtonPress ||
			event->type() == QEvent::MouseButtonRelease ||
			event->type() == QEvent::MouseMove ||
			event->type() == QEvent::Wheel)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (m_btnCancelDraw && m_btnCancelDraw->geometry().contains(me->pos()))
				return false;
			event->accept();
			return true;
		}
		return false;
	}

	if (obj == ui.canvas_view_main->viewport())
	{
		if (event->type() == QEvent::Wheel)
		{
			QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
			if (!(wheel->modifiers() & Qt::ControlModifier)) { event->accept(); return true; }
			QPointF oldPos = ui.canvas_view_main->mapToScene(wheel->position().toPoint());
			m_scaleValue *= (wheel->angleDelta().y() > 0 ? ScaleConfig::WheelStepUp : ScaleConfig::WheelStepDown);
			m_scaleValue = ScaleConfig::clamp(m_scaleValue);

			QTransform trans;
			trans.scale(m_scaleValue, m_scaleValue);
			ui.canvas_view_main->setTransform(trans);

			QPointF newPos = ui.canvas_view_main->mapToScene(wheel->position().toPoint());
			QPointF delta = oldPos - newPos;  // 补偿缩放原点偏移
			ui.canvas_view_main->horizontalScrollBar()->setValue(
				ui.canvas_view_main->horizontalScrollBar()->value() + qRound(delta.x()));
			ui.canvas_view_main->verticalScrollBar()->setValue(
				ui.canvas_view_main->verticalScrollBar()->value() + qRound(delta.y()));

			ui.info_lab_scale_ratio->setText(QString("%1%").arg(qRound(m_scaleValue * 100)));
			event->accept();
			return true;
		}

		if (event->type() != QEvent::MouseButtonPress &&
			event->type() != QEvent::MouseButtonRelease &&
			event->type() != QEvent::MouseMove &&
			event->type() != QEvent::Leave)
		{
			return QMainWindow::eventFilter(obj, event);
		}

		QPointF scenePos;
		if (event->type() != QEvent::Leave)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			scenePos = ui.canvas_view_main->mapToScene(me->pos());
		}

		if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			updateInfoLabel(scenePos, me->globalPosition().toPoint());
		}
		else if (event->type() == QEvent::Leave)
		{
			m_infoLabel->setVisible(false);
			ui.canvas_view_main->viewport()->update();  // 强制 GL 视口重绘，清除叠加标签隐藏后的边缘残留
		}

		// ===== 绘制模式 =====
		if (m_mode == Mode_Draw)
		{
		// 第一步按下时，吸收线移到点击位置吸收残影
		//if (event->type() == QEvent::MouseButtonPress && m_drawStep == 0)
		//{
		//	QMouseEvent* me = static_cast<QMouseEvent*>(event);
		//	if (me->button() == Qt::LeftButton && m_spotAbsorber)
		//	{
		//		m_spotAbsorber->setLine(scenePos.x(), scenePos.y(), scenePos.x(), scenePos.y()+0.01);
		//		ui.canvas_view_main->viewport()->repaint();
		//	}
		//}
		// ===== 四点扇环 =====
		// 点1=起点A, 点2=终点B, 点3=锁r1, 点4=锁r2
		if (m_currentShape == Shape_Arc)
		{
			// 中键按下 → 画布平移
		if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::MiddleButton)
			{
			lastViewPos = me->pos();
				isPanning = true;
				ui.canvas_view_main->setCursor(Qt::ClosedHandCursor);
				event->accept();
				return true;
			}
		}

		if (event->type() == QEvent::MouseButtonPress)
			{
				QMouseEvent* me = static_cast<QMouseEvent*>(event);
				if (me->button() == Qt::LeftButton)
				{
					if (m_drawStep == 0) {
						m_circlePt1 = scenePos; m_drawStep = 1;
						double mrkR = 3.0;
						m_circleMarker1 = new QGraphicsEllipseItem(m_circlePt1.x()-mrkR,m_circlePt1.y()-mrkR,mrkR*2,mrkR*2);
						m_circleMarker1->setPen(Qt::NoPen);m_circleMarker1->setBrush(QColor(255,80,80));m_circleMarker1->setZValue(100);m_scene->addItem(m_circleMarker1);
					}else if(m_drawStep == 1){
						m_circlePt2 = scenePos; m_drawStep = 2;
						double mrkR = 3.0;
						m_circleMarker2 = new QGraphicsEllipseItem(m_circlePt2.x()-mrkR,m_circlePt2.y()-mrkR,mrkR*2,mrkR*2);
						m_circleMarker2->setPen(Qt::NoPen);m_circleMarker2->setBrush(QColor(255,80,80));m_circleMarker2->setZValue(100);m_scene->addItem(m_circleMarker2);
					}else if(m_drawStep == 2){
						// 第三点锁r1
						QPointF A(m_circlePt1),B(m_circlePt2),C(scenePos),O;double r1;
						if(ShapeGeometry::circumcircle(A,B,C,O,r1)&&r1>1.5){
							double aA=ShapeGeometry::normAngle360(qRadiansToDegrees(std::atan2(A.y()-O.y(),A.x()-O.x())));
							double aB=ShapeGeometry::normAngle360(qRadiansToDegrees(std::atan2(B.y()-O.y(),B.x()-O.x())));
							double aC=ShapeGeometry::normAngle360(qRadiansToDegrees(std::atan2(C.y()-O.y(),C.x()-O.x())));
							double sf=aB-aA;if(sf<=0)sf+=360.0;double rc=aC-aA;if(rc<0)rc+=360.0;
							double sp=(rc<=sf)?sf:(sf-360.0);
							m_anchorPoint=O;m_arcStartAngle=aA;m_arcEndAngle=sp;
							m_dragStartRect.cx=O.x();m_dragStartRect.cy=O.y();m_dragStartRect.w=r1;
							m_drawStep=3;
						}
					}else if(m_drawStep == 3){
						commitArc();
						if(m_ghostArcPath){m_scene->removeItem(m_ghostArcPath);delete m_ghostArcPath;m_ghostArcPath=nullptr;}
						if(m_ghostEllipse2){m_scene->removeItem(m_ghostEllipse2);delete m_ghostEllipse2;m_ghostEllipse2=nullptr;}
						if(m_circleMarker1){m_scene->removeItem(m_circleMarker1);delete m_circleMarker1;m_circleMarker1=nullptr;}
						if(m_circleMarker2){m_scene->removeItem(m_circleMarker2);delete m_circleMarker2;m_circleMarker2=nullptr;}
						hideDrawModeOverlay();m_mode=Mode_None;m_drawStep=0;ui.canvas_view_main->setCursor(Qt::ArrowCursor);
					}
					event->accept();return true;
				}else if(me->button()==Qt::RightButton){stopDraw();event->accept();return true;}
			}
			else if(event->type()==QEvent::MouseMove&&m_drawStep>=2&&m_drawStep<=3)
			{
				QPointF A(m_circlePt1),B(m_circlePt2);
				if(m_drawStep==2){
					QPointF C(scenePos),O;double r;
					if(ShapeGeometry::circumcircle(A,B,C,O,r)&&r>1.5){
						double aA=ShapeGeometry::normAngle360(qRadiansToDegrees(std::atan2(A.y()-O.y(),A.x()-O.x())));
						double aB=ShapeGeometry::normAngle360(qRadiansToDegrees(std::atan2(B.y()-O.y(),B.x()-O.x())));
						double aC=ShapeGeometry::normAngle360(qRadiansToDegrees(std::atan2(C.y()-O.y(),C.x()-O.x())));
						double sf=aB-aA;if(sf<=0)sf+=360.0;double rc=aC-aA;if(rc<0)rc+=360.0;
						double sp=(rc<=sf)?sf:(sf-360.0);
						if(!m_ghostArcPath){m_ghostArcPath=new QGraphicsPathItem();QPen p(QColor(0,180,255),m_penWidth,Qt::DashLine);p.setCosmetic(true);m_ghostArcPath->setPen(p);m_ghostArcPath->setZValue(99);m_scene->addItem(m_ghostArcPath);}
						QPainterPath ph;const int N=128;double ar=qDegreesToRadians(aA),sr=qDegreesToRadians(sp);
						ph.moveTo(O.x()+r*qCos(ar),O.y()+r*qSin(ar));
						for(int i=1;i<=N;++i){double ang=ar+sr*i/N;ph.lineTo(O.x()+r*qCos(ang),O.y()+r*qSin(ang));}
						m_ghostArcPath->setPath(ph);
					}
				}else{
					QPointF O=m_anchorPoint;double aA=m_arcStartAngle,sp=m_arcEndAngle,r1=m_dragStartRect.w;
					double r2=std::sqrt((scenePos.x()-O.x())*(scenePos.x()-O.x())+(scenePos.y()-O.y())*(scenePos.y()-O.y()));r2=std::max(r2,2.0);
					double rO=std::max(r1,r2),rI=std::min(r1,r2);
					if(!m_ghostArcPath){m_ghostArcPath=new QGraphicsPathItem();QPen p(QColor(0,180,255),m_penWidth,Qt::DashLine);p.setCosmetic(true);m_ghostArcPath->setPen(p);m_ghostArcPath->setZValue(99);m_scene->addItem(m_ghostArcPath);}
					double aR=qDegreesToRadians(aA),sR=qDegreesToRadians(sp);QPainterPath ph;const int N=128;
					ph.moveTo(O.x()+rO*qCos(aR),O.y()+rO*qSin(aR));
					for(int i=1;i<=N;++i){double ang=aR+sR*i/N;ph.lineTo(O.x()+rO*qCos(ang),O.y()+rO*qSin(ang));}
					double eR=aR+sR;ph.lineTo(O.x()+rI*qCos(eR),O.y()+rI*qSin(eR));
					for(int i=N;i>=0;--i){double ang=aR+sR*i/N;ph.lineTo(O.x()+rI*qCos(ang),O.y()+rI*qSin(ang));}
					ph.closeSubpath();m_ghostArcPath->setPath(ph);
					m_dragStartRect.h=r2;
				}
				event->accept();return true;
			}
			return false;
		}

		// ===== 多边形 =====
		if (m_currentShape == Shape_Polygon)
		{
			// 中键按下 → 画布平移
		if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::MiddleButton)
			{
			lastViewPos = me->pos();
				isPanning = true;
				ui.canvas_view_main->setCursor(Qt::ClosedHandCursor);
				event->accept();
				return true;
			}
		}

		if (event->type() == QEvent::MouseButtonPress)
			{
				QMouseEvent* me = static_cast<QMouseEvent*>(event);
				if (me->button() == Qt::LeftButton) {
					// 点击第一个顶点附近且 ≥3 点 → 闭合
					if (m_tempPolyPts.size() >= 3) {
						QPointF d = scenePos - m_tempPolyPts.first();
						if (std::sqrt(d.x()*d.x()+d.y()*d.y()) < 15.0) {
							commitPolygon(); event->accept(); return true;
						}
					}
					m_tempPolyPts.append(scenePos);
					double mrkR = 3.0;
					auto* mk = new QGraphicsEllipseItem(scenePos.x()-mrkR,scenePos.y()-mrkR,mrkR*2,mrkR*2);
					mk->setPen(Qt::NoPen);mk->setBrush(QColor(255,80,80));mk->setZValue(100);m_scene->addItem(mk);m_circleMarkers.append(mk);
					m_drawStep = 1;
				}
				event->accept(); return true;
			}
			else if (event->type() == QEvent::MouseMove && m_drawStep == 1 && m_tempPolyPts.size() >= 1)
			{
				// 已绘边预览
				if (!m_ghostArcPath) { m_ghostArcPath=new QGraphicsPathItem(); QPen pen(QColor(0,180,255),m_penWidth,Qt::DashLine); pen.setCosmetic(true); m_ghostArcPath->setPen(pen); m_ghostArcPath->setZValue(99); m_scene->addItem(m_ghostArcPath); }
				QPainterPath edges; edges.moveTo(m_tempPolyPts.first());
				for(int i=1;i<m_tempPolyPts.size();++i) edges.lineTo(m_tempPolyPts[i]);
				edges.lineTo(scenePos);
				m_ghostArcPath->setPath(edges);

				// 靠近起点时高亮闭合线
				QPointF d = scenePos - m_tempPolyPts.first();
				double dist = std::sqrt(d.x()*d.x()+d.y()*d.y());
				if (m_ghostPolyLine) { m_ghostPolyLine->setVisible(dist < 15.0 && m_tempPolyPts.size()>=3); }
				else if (dist < 15.0 && m_tempPolyPts.size()>=3) {
					m_ghostPolyLine = new QGraphicsLineItem(); QPen p(QColor(255,200,0),m_penWidth+1,Qt::DashLine); p.setCosmetic(true);
					m_ghostPolyLine->setPen(p); m_ghostPolyLine->setZValue(100); m_scene->addItem(m_ghostPolyLine);
				}
				if (m_ghostPolyLine && m_ghostPolyLine->isVisible())
					m_ghostPolyLine->setLine(QLineF(m_tempPolyPts.last(), m_tempPolyPts.first()));

				event->accept(); return true;
			}
			return false;
		}

		if (m_currentShape == Shape_Circle || m_currentShape == Shape_Ring)
		{
			// 圆环：四点构造，前三点确定圆（同圆形），第四点决定另一个圆的半径
			// step0→step1: 点1
			// step1→step2: 点1+点2+鼠标=三角形外心画ghost圆
			// step2→step3: 确定第一个圆，第四点决定第二个半径
			// step3→确认: 提交
			// 中键按下 → 画布平移
		if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::MiddleButton)
			{
			lastViewPos = me->pos();
				isPanning = true;
				ui.canvas_view_main->setCursor(Qt::ClosedHandCursor);
				event->accept();
				return true;
			}
		}

		if (event->type() == QEvent::MouseButtonPress)
			{
				QMouseEvent* me = static_cast<QMouseEvent*>(event);
				if (me->button() == Qt::LeftButton)
				{
					if (m_drawStep == 0)
					{
						m_circlePt1 = scenePos;
						m_drawStep = 1;
						double mrkR = 3.0;
						m_circleMarker1 = new QGraphicsEllipseItem(
							m_circlePt1.x()-mrkR, m_circlePt1.y()-mrkR, mrkR*2, mrkR*2);
						m_circleMarker1->setPen(Qt::NoPen);
						m_circleMarker1->setBrush(QColor(255, 80, 80));
						m_circleMarker1->setZValue(100);
						m_scene->addItem(m_circleMarker1);
					}
					else if (m_drawStep == 1)
					{
						m_circlePt2 = scenePos;
						m_drawStep = 2;
						double mrkR = 3.0;
						m_circleMarker2 = new QGraphicsEllipseItem(
							m_circlePt2.x()-mrkR, m_circlePt2.y()-mrkR, mrkR*2, mrkR*2);
						m_circleMarker2->setPen(Qt::NoPen);
						m_circleMarker2->setBrush(QColor(255, 80, 80));
						m_circleMarker2->setZValue(100);
						m_scene->addItem(m_circleMarker2);
					}
					else if (m_drawStep == 2)
					{
						// 第三步：三点外心确定圆
						// 先计算并固定第一个圆的半径和中心
						double x1=m_circlePt1.x(), y1=m_circlePt1.y();
						double x2=m_circlePt2.x(), y2=m_circlePt2.y();
						double x3=scenePos.x(),   y3=scenePos.y();
						double d = 2.0*(x1*(y2-y3) + x2*(y3-y1) + x3*(y1-y2));
						if (std::abs(d) < 1e-6) { event->accept(); return true; }
						double ux = ((x1*x1+y1*y1)*(y2-y3) + (x2*x2+y2*y2)*(y3-y1) + (x3*x3+y3*y3)*(y1-y2)) / d;
						double uy = ((x1*x1+y1*y1)*(x3-x2) + (x2*x2+y2*y2)*(x1-x3) + (x3*x3+y3*y3)*(x2-x1)) / d;
						double r  = std::sqrt((x1-ux)*(x1-ux) + (y1-uy)*(y1-uy));

						if (m_currentShape == Shape_Circle)
						{
							commitCircle();
							if (m_ghostEllipse) { m_scene->removeItem(m_ghostEllipse); delete m_ghostEllipse; m_ghostEllipse = nullptr; }
							if (m_circleMarker1) { m_scene->removeItem(m_circleMarker1); delete m_circleMarker1; m_circleMarker1 = nullptr; }
							if (m_circleMarker2) { m_scene->removeItem(m_circleMarker2); delete m_circleMarker2; m_circleMarker2 = nullptr; }
							hideDrawModeOverlay();
							m_mode = Mode_None;
							m_drawStep = 0;
							ui.canvas_view_main->setCursor(Qt::ArrowCursor);
						}
						else // Shape_Ring: 进入第四步
						{
							// 保存第一个圆的中心和半径
							m_anchorPoint = QPointF(ux, uy);
							m_dragStartRect.cx = ux;
							m_dragStartRect.cy = uy;
							m_dragStartRect.w = r * 2;  // 存第一个半径在 w 里
							m_drawStep = 3;
							// 更新 ghost ellipse 为已固定的圆（实线预览）
							if (m_ghostEllipse)
							{
								m_ghostEllipse->setRect(ux-r, uy-r, r*2, r*2);
								QPen solidPen(QColor(0, 180, 255), m_penWidth);
								solidPen.setCosmetic(true);
								m_ghostEllipse->setPen(solidPen);
							}
							// 创建第二个 ghost ellipse（内圆预览）
							if (!m_ghostEllipse2)
							{
								m_ghostEllipse2 = new QGraphicsEllipseItem();
								QPen ghostPen2(QColor(255, 140, 0), m_penWidth, Qt::DashLine);
								ghostPen2.setCosmetic(true);
								m_ghostEllipse2->setPen(ghostPen2);
								m_ghostEllipse2->setBrush(QColor(255, 140, 0, 20));
								m_ghostEllipse2->setZValue(98);
								m_scene->addItem(m_ghostEllipse2);
							}
						}
					}
					else if (m_drawStep == 3 && m_currentShape == Shape_Ring)
					{
						// 第四步：确定第二个圆的半径，提交圆环
						commitRing();
						if (m_ghostEllipse) { m_scene->removeItem(m_ghostEllipse); delete m_ghostEllipse; m_ghostEllipse = nullptr; }
						if (m_ghostEllipse2) { m_scene->removeItem(m_ghostEllipse2); delete m_ghostEllipse2; m_ghostEllipse2 = nullptr; }
						if (m_circleMarker1) { m_scene->removeItem(m_circleMarker1); delete m_circleMarker1; m_circleMarker1 = nullptr; }
						if (m_circleMarker2) { m_scene->removeItem(m_circleMarker2); delete m_circleMarker2; m_circleMarker2 = nullptr; }
						hideDrawModeOverlay();
						m_mode = Mode_None;
						m_drawStep = 0;
						ui.canvas_view_main->setCursor(Qt::ArrowCursor);
					}
					event->accept();
					return true;
				}
				else if (me->button() == Qt::RightButton)
				{
					stopDraw();
					event->accept();
					return true;
				}
			}
			else if (event->type() == QEvent::MouseMove && m_drawStep >= 1 && m_drawStep <= 3)
			{
				// 确保 ghost ellipse 存在
				if (!m_ghostEllipse)
				{
					m_ghostEllipse = new QGraphicsEllipseItem();
					QPen ghostPen(QColor(0, 180, 255), m_penWidth, Qt::DashLine);
					ghostPen.setCosmetic(true);
					m_ghostEllipse->setPen(ghostPen);
					m_ghostEllipse->setBrush(QColor(0, 180, 255, 30));
					m_ghostEllipse->setZValue(99);
					m_scene->addItem(m_ghostEllipse);
				}

				if (m_drawStep == 1)
				{
					// 两点模式：以点1到鼠标为直径的圆
					double d1x = m_circlePt1.x(), d1y = m_circlePt1.y();
					double mx  = scenePos.x(), my = scenePos.y();
					double cx = (d1x + mx) / 2.0;
					double cy = (d1y + my) / 2.0;
					double r  = std::sqrt((mx-d1x)*(mx-d1x) + (my-d1y)*(my-d1y)) / 2.0;
					m_ghostEllipse->setRect(cx-r, cy-r, r*2, r*2);
				}
				else if (m_drawStep == 2)
				{
					// 三点模式：三角形外心
					double x1=m_circlePt1.x(), y1=m_circlePt1.y();
					double x2=m_circlePt2.x(), y2=m_circlePt2.y();
					double x3=scenePos.x(),   y3=scenePos.y();
					double d = 2.0*(x1*(y2-y3) + x2*(y3-y1) + x3*(y1-y2));
					if (std::abs(d) < 1e-6) { event->accept(); return true; }
					double ux = ((x1*x1+y1*y1)*(y2-y3) + (x2*x2+y2*y2)*(y3-y1) + (x3*x3+y3*y3)*(y1-y2)) / d;
					double uy = ((x1*x1+y1*y1)*(x3-x2) + (x2*x2+y2*y2)*(x1-x3) + (x3*x3+y3*y3)*(x2-x1)) / d;
					double r  = std::sqrt((x1-ux)*(x1-ux) + (y1-uy)*(y1-uy));
					m_ghostEllipse->setRect(ux-r, uy-r, r*2, r*2);
				}
				else if (m_drawStep == 3 && m_currentShape == Shape_Ring)
				{
					// 第四步预览第二个圆：中心已定，鼠标距中心的距离就是第二个半径
					double cx = m_anchorPoint.x(), cy = m_anchorPoint.y();
					double r2 = std::sqrt((scenePos.x()-cx)*(scenePos.x()-cx) + (scenePos.y()-cy)*(scenePos.y()-cy));
					if (m_ghostEllipse2)
						m_ghostEllipse2->setRect(cx-r2, cy-r2, r2*2, r2*2);
				}
				event->accept();
				return true;
			}
			return false;
		}

			// 通用两步绘制（矩形、旋转矩形、椭圆等）
			// 中键按下 → 画布平移
		if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::MiddleButton)
			{
			lastViewPos = me->pos();
				isPanning = true;
				ui.canvas_view_main->setCursor(Qt::ClosedHandCursor);
				event->accept();
				return true;
			}
		}

		if (event->type() == QEvent::MouseButtonPress)
			{
				QMouseEvent* me = static_cast<QMouseEvent*>(event);
				if (me->button() == Qt::LeftButton)
				{
					if (m_drawStep == 0)
					{
						m_anchorPoint = scenePos;
						m_drawStep = 1;
						m_ghostRect = new QGraphicsRectItem();
						QPen ghostPen(QColor(0, 180, 255), m_penWidth, Qt::DashLine);
						ghostPen.setCosmetic(true);
						m_ghostRect->setPen(ghostPen);
						m_ghostRect->setBrush(QColor(0, 180, 255, 30));
						m_ghostRect->setZValue(99);
						m_ghostRect->setRect(QRectF(m_anchorPoint, QSizeF(0, 0)));
						m_scene->addItem(m_ghostRect);
						// 椭圆的 ghost ellipse
						if (m_currentShape == Shape_Ellipse)
						{
							m_ghostEllipse = new QGraphicsEllipseItem();
							QPen ghostEPen(QColor(0, 180, 255), m_penWidth, Qt::DashLine);
							ghostEPen.setCosmetic(true);
							m_ghostEllipse->setPen(ghostEPen);
							m_ghostEllipse->setBrush(QColor(0, 180, 255, 20));
							m_ghostEllipse->setZValue(99);
							m_ghostEllipse->setRect(m_ghostRect->rect());
							m_scene->addItem(m_ghostEllipse);
						}
					}
					else if (m_drawStep == 1)
					{
						commitShapeGeneric(m_currentShape);
						if (m_ghostRect) { m_scene->removeItem(m_ghostRect); delete m_ghostRect; m_ghostRect = nullptr; }
						if (m_ghostEllipse) { m_scene->removeItem(m_ghostEllipse); delete m_ghostEllipse; m_ghostEllipse = nullptr; }
						hideDrawModeOverlay();
						m_mode = Mode_None;
						m_drawStep = 0;
						ui.canvas_view_main->setCursor(Qt::ArrowCursor);
					}
					event->accept();
					return true;
				}
				else if (me->button() == Qt::RightButton)
				{
					stopDraw();
					event->accept();
					return true;
				}
			}
			else if (event->type() == QEvent::MouseMove && (m_drawStep == 0 || m_drawStep == 1))
			{
				if (m_drawStep == 1)
				{
					updateGhostRect(scenePos);
					if (m_currentShape == Shape_Ellipse && m_ghostEllipse && m_ghostRect)
						m_ghostEllipse->setRect(m_ghostRect->rect());
				}
				event->accept();
				return true;
			}
			return false;
		}

		// ===== 非绘制模式 =====

		// MouseButtonRelease（停止拖拽）
		if (event->type() == QEvent::MouseButtonRelease)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::LeftButton)
			{
				if (m_isRotating)
				{
					m_isRotating = false;
					m_dragShapeIndex = -1;
					m_dragHandleIndex = -1;
					ui.canvas_view_main->setCursor(Qt::CrossCursor);
					event->accept();
					return true;
				}
				if (m_isDraggingHandle)
				{
					m_isDraggingHandle = false;
					m_dragShapeIndex = -1;
					m_dragHandleIndex = -1;
					ui.canvas_view_main->setCursor(Qt::CrossCursor);
					event->accept();
					return true;
				}
				if (m_isDraggingShape)
				{
					m_isDraggingShape = false;
					ui.canvas_view_main->setCursor(Qt::ArrowCursor);
					event->accept();
					return true;
				}
				if (isPanning)
				{
					isPanning = false;
					ui.canvas_view_main->setCursor(Qt::ArrowCursor);
					return true;
				}
			}
			if (me->button() == Qt::MiddleButton && isPanning)
			{
				isPanning = false;
				ui.canvas_view_main->setCursor(Qt::ArrowCursor);
				event->accept();
				return true;
			}
		}

		// 旋转拖拽中
		if (m_isRotating && event->type() == QEvent::MouseMove && m_dragShapeIndex >= 0)
		{
			{
				DrawShapeItem& ds = m_shapes[m_dragShapeIndex];
				EditContext ctx{ m_dragStartRect, m_dragHandleIndex, m_isRotating, m_isDraggingHandle, m_dragStartAngle };
				bool changed = false;
				if (ds.type == Shape_Ellipse)
					changed = ShapeEditor::updateEllipse(&ds, ctx, scenePos);
				else if (ds.type == Shape_Arc)
					changed = ShapeEditor::updateArc(&ds, ctx, scenePos);
				else
					changed = ShapeEditor::updateRotatedRect(&ds, ctx, scenePos);
				if (changed) refreshActiveShape();
			}
			event->accept();
			return true;
		}
		// Handle 拖拽中
			if (m_isDraggingHandle && event->type() == QEvent::MouseMove && m_dragShapeIndex >= 0)
			{
				{
					DrawShapeItem& ds = m_shapes[m_dragShapeIndex];
					EditContext ctx{ m_dragStartRect, m_dragHandleIndex, m_isRotating, m_isDraggingHandle, m_dragStartAngle };
					bool changed = false;
					if (ds.type == Shape_RotateRect)
						changed = ShapeEditor::updateRotatedRect(&ds, ctx, scenePos);
					else if (ds.type == Shape_Circle)
						changed = ShapeEditor::updateCircle(&ds, ctx, scenePos);
					else if (ds.type == Shape_Ellipse)
						changed = ShapeEditor::updateEllipse(&ds, ctx, scenePos);
					else if (ds.type == Shape_Ring)
						changed = ShapeEditor::updateRing(&ds, ctx, scenePos);
					else if (ds.type == Shape_Arc)
						changed = ShapeEditor::updateArc(&ds, ctx, scenePos);
					else if (ds.type == Shape_Polygon)
						changed = ShapeEditor::updatePolygon(&ds, ctx, scenePos);
					else
						changed = ShapeEditor::updateRect(&ds, ctx, scenePos);
					if (changed) refreshActiveShape();
				}
				event->accept();
				return true;
			}
		// 形状拖动中
		if (m_isDraggingShape && event->type() == QEvent::MouseMove && m_activeShapeIndex >= 0)
		{
			DrawShapeItem& shape = m_shapes[m_activeShapeIndex];
			if (shape.type == Shape_Rect)
			{
				shape.cx = scenePos.x() - m_dragOffset.x();
				shape.cy = scenePos.y() - m_dragOffset.y();
			}
			else if (shape.type == Shape_RotateRect)
			{
				shape.cx = scenePos.x() - m_dragOffset.x();
				shape.cy = scenePos.y() - m_dragOffset.y();
			}
			else if (shape.type == Shape_Circle)
			{
				shape.cx = scenePos.x() - m_dragOffset.x();
				shape.cy = scenePos.y() - m_dragOffset.y();
			}
			else if (shape.type == Shape_Ellipse)
			{
				shape.cx = scenePos.x() - m_dragOffset.x();
				shape.cy = scenePos.y() - m_dragOffset.y();
			}
			else if (shape.type == Shape_Ring)
			{
				shape.cx = scenePos.x() - m_dragOffset.x();
				shape.cy = scenePos.y() - m_dragOffset.y();
			}
			else if (shape.type == Shape_Arc)
			{
				shape.cx = scenePos.x() - m_dragOffset.x();
				shape.cy = scenePos.y() - m_dragOffset.y();
			}
			else if (shape.type == Shape_Polygon)
			{
				double dx = scenePos.x() - m_dragOffset.x() - shape.pts[0].x();
				double dy = scenePos.y() - m_dragOffset.y() - shape.pts[0].y();
				for(auto& pt:shape.pts){pt.rx()+=dx;pt.ry()+=dy;}
			}
			else
			{
				shape.cx = scenePos.x() - m_dragOffset.x();
				shape.cy = scenePos.y() - m_dragOffset.y();
			}
			syncParamPanel(m_activeShapeIndex);
			m_handleHelper->updatePositions(shape, *m_activeHandleSet, m_shapeItem);
			event->accept();
			return true;
		}

		// 中键按下 → 画布平移
		if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::MiddleButton)
			{
			lastViewPos = me->pos();
				isPanning = true;
				ui.canvas_view_main->setCursor(Qt::ClosedHandCursor);
				event->accept();
				return true;
			}
		}

		if (event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (me->button() == Qt::LeftButton && m_activeShapeIndex >= 0)
			{
				DrawShapeItem& active = m_shapes[m_activeShapeIndex];
				// 优先检测 handle
				QGraphicsEllipseItem* hitHandle = handleAt(scenePos);
				if (hitHandle)
				{
					int hIdx = hitHandle->data(0).toInt();
					int role = hitHandle->data(1).toInt();
					if (role == 1) // 旋转控制点
					{
						m_isRotating = true;
						m_dragShapeIndex = m_activeShapeIndex;
						m_dragHandleIndex = hIdx;
						if (active.type == Shape_Arc)
							m_dragStartAngle = ShapeGeometry::normAngle360(active.startAngle + active.span / 2.0);
						else
							m_dragStartAngle = active.angle;
						ui.canvas_view_main->setCursor(Qt::ClosedHandCursor);
						event->accept();
						return true;
					}
				if (hIdx >= 0 && m_activeHandleSet && hIdx < m_activeHandleSet->handles.size())
				{
					m_isDraggingHandle = true;
					m_dragShapeIndex = m_activeShapeIndex;
					m_dragHandleIndex = hIdx;
					if (active.type == Shape_RotateRect)
					{
						m_dragStartRect = { active.cx, active.cy,
						                    active.w, active.h };
						m_dragStartAngle = active.angle;
					}
					else if (active.type == Shape_Circle)
						m_dragStartRect = { active.cx, active.cy,
						                    active.r * 2, active.r * 2 };
					else if (active.type == Shape_Ellipse)
						m_dragStartRect = { active.cx, active.cy,
						                    active.r * 2, active.r2 * 2 };
					else if (active.type == Shape_Ring)
					{
						m_dragStartRect = { active.cx, active.cy,
						                    active.r * 2, active.r2 * 2 };
					}
					else if (active.type == Shape_Arc)
					{
						m_dragStartRect = { active.cx, active.cy,
						                    active.startAngle, active.endAngle };
					}
					else if (active.type == Shape_Polygon)
					{
						m_dragStartRect = { 0,0,0,0 };
					}
					else
						m_dragStartRect = { active.cx, active.cy,
						                    active.w, active.h };
					ui.canvas_view_main->setCursor(Qt::ClosedHandCursor);
						event->accept();
						return true;
					}
				}

				// 检测形状内部拖动
			if (ShapeGeometry::contains(&active, scenePos))
			{
			m_isDraggingShape = true;
			if (active.type == Shape_RotateRect)
				m_dragOffset = QPointF(scenePos.x() - active.cx,
				                       scenePos.y() - active.cy);
			else if (active.type == Shape_Circle)
				m_dragOffset = QPointF(scenePos.x() - active.cx,
				                       scenePos.y() - active.cy);
			else if (active.type == Shape_Ellipse)
				m_dragOffset = QPointF(scenePos.x() - active.cx,
				                       scenePos.y() - active.cy);
			else if (active.type == Shape_Ring)
				m_dragOffset = QPointF(scenePos.x() - active.cx,
				                       scenePos.y() - active.cy);
			else if (active.type == Shape_Arc)
				m_dragOffset = QPointF(scenePos.x() - active.cx,
				                       scenePos.y() - active.cy);
			else if (active.type == Shape_Polygon)
				m_dragOffset = QPointF(scenePos.x() - active.pts[0].x(),
				                       scenePos.y() - active.pts[0].y());
			else
				m_dragOffset = QPointF(scenePos.x() - active.cx,
				                       scenePos.y() - active.cy);
				event->accept();
				return true;
			}
			}

		}
		else if (event->type() == QEvent::MouseMove)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if (isPanning && (me->buttons() & Qt::MiddleButton))
			{
				QPoint delta = me->pos() - lastViewPos;
				lastViewPos = me->pos();
				ui.canvas_view_main->horizontalScrollBar()->setValue(
					ui.canvas_view_main->horizontalScrollBar()->value() - delta.x());
				ui.canvas_view_main->verticalScrollBar()->setValue(
					ui.canvas_view_main->verticalScrollBar()->value() - delta.y());
			}
			else if (!isPanning)
			{
				// Handle hover
				QGraphicsEllipseItem* hoverHandle = handleAt(scenePos);
				if (hoverHandle && m_activeShapeIndex >= 0)
				{
					ui.canvas_view_main->setCursor(Qt::SizeAllCursor);
				}
				else
				{
					// 形状本体悬停加粗
					bool nowHovered = m_activeShapeIndex >= 0
						&& ShapeGeometry::contains(&m_shapes[m_activeShapeIndex], scenePos);
					if (nowHovered != m_isShapeHovered) {
						applyShapeHover(nowHovered);
						m_isShapeHovered = nowHovered;
					}
					ui.canvas_view_main->setCursor(Qt::ArrowCursor);
				}
			}
		}
	}

	return QMainWindow::eventFilter(obj, event);
}

// ===== 键盘 =====

void ImageCanvasView::keyPressEvent(QKeyEvent* event)
{
	if (m_mode == Mode_Draw && event->key() == Qt::Key_Escape)
	{
		stopDraw();
		ui.draw_cbox_shape_type->blockSignals(true);
		ui.draw_cbox_shape_type->setCurrentIndex(0);
		ui.draw_cbox_shape_type->blockSignals(false);
		return;
	}

	if (event->key() == Qt::Key_Delete && m_activeShapeIndex >= 0)
	{
		clearSceneShape();
		m_shapes.removeAt(m_activeShapeIndex);
		m_isShapeHovered = false;
		m_activeShapeIndex = -1;
		return;
	}

	QMainWindow::keyPressEvent(event);
}

// ===== 缩放 =====

void ImageCanvasView::updateScaleUI()
{
	m_scaleValue = ScaleConfig::clamp(m_scaleValue);
	m_toolbar->setScale(m_scaleValue);
	QTransform t;
	t.scale(m_scaleValue, m_scaleValue);
	ui.canvas_view_main->setTransform(t);
}

void ImageCanvasView::slotZoomIn()   { m_scaleValue = ScaleConfig::clamp(m_scaleValue + ScaleConfig::ButtonStep); m_toolbar->setScale(m_scaleValue); ui.canvas_view_main->setTransform(QTransform::fromScale(m_scaleValue, m_scaleValue)); }
void ImageCanvasView::slotZoomOut()  { m_scaleValue = ScaleConfig::clamp(m_scaleValue - ScaleConfig::ButtonStep); m_toolbar->setScale(m_scaleValue); ui.canvas_view_main->setTransform(QTransform::fromScale(m_scaleValue, m_scaleValue)); }
void ImageCanvasView::slotZoomReset(){ m_scaleValue = ScaleConfig::DefaultScale; m_toolbar->setScale(ScaleConfig::DefaultScale); ui.canvas_view_main->setTransform(QTransform::fromScale(ScaleConfig::DefaultScale, ScaleConfig::DefaultScale)); }

void ImageCanvasView::slotZoomFit()
{
	QPixmap pm = m_pixmapItem->pixmap();
	if (pm.isNull()) return;
	QRectF imageRect = pm.rect();
	ui.canvas_view_main->fitInView(imageRect, Qt::KeepAspectRatio);
	m_scaleValue = ScaleConfig::clamp(ui.canvas_view_main->transform().m11());
	m_toolbar->setScale(m_scaleValue);
}

void ImageCanvasView::slotToggleCenterCross()
{
	m_showCenterCross = !m_showCenterCross;
	m_crossH->setVisible(m_showCenterCross);
	m_crossV->setVisible(m_showCenterCross);
	m_toolbar->updateCrossBtnText(m_showCenterCross);
	updateCenterCross();
}

void ImageCanvasView::slotToggleLineWidth()
{
	m_thinLine = !m_thinLine;
	m_penWidth = m_thinLine ? 0.5 : 2.0;
	m_toolbar->updateLineWidthBtnText(m_thinLine);
	if (m_shapeItem)
		m_painter->applyStyle(m_shapeItem, m_colorSelected, m_penWidth, m_isShapeHovered);
}

void ImageCanvasView::slotToggleControlPoints()
{
	m_showControlPoints = !m_showControlPoints;
	m_toolbar->updateCtrlPtBtnText(m_showControlPoints);
	if (m_activeHandleSet)
		m_handleHelper->setHandlesVisible(*m_activeHandleSet, m_showControlPoints);
}

void ImageCanvasView::updateCenterCross()
{
	QPixmap pm = m_pixmapItem->pixmap();
	if (pm.isNull()) return;
	double w = pm.width(), h = pm.height();
	m_crossH->setLine(0, h / 2.0, w, h / 2.0);
	m_crossV->setLine(w / 2.0, 0, w / 2.0, h);
}

void ImageCanvasView::updateInfoLabel(const QPointF& scenePos, const QPoint& globalPos)
{
	double mouseX = scenePos.x(), mouseY = scenePos.y();
	QPixmap pm = m_pixmapItem->pixmap();
	bool inImage = !pm.isNull() && mouseX >= 0.0 && mouseX < pm.width()
		&& mouseY >= 0.0 && mouseY < pm.height();
	QString text;
	if (inImage)
	{
		QImage img = pm.toImage();
		int x = static_cast<int>(floor(mouseX)), y = static_cast<int>(floor(mouseY));
		QRgb rgb = img.pixel(x, y);
		int r = qRed(rgb), g = qGreen(rgb), b = qBlue(rgb);
		int grayVal = qRound(r * 0.299 + g * 0.587 + b * 0.114);
		text = QString("X:%1 Y:%2  R:%3 G:%4 B:%5  Gray:%6")
			.arg(mouseX, 0, 'f', 2).arg(mouseY, 0, 'f', 2).arg(r).arg(g).arg(b).arg(grayVal);
	}
	else { text = QString("X:%1 Y:%2").arg(mouseX, 0, 'f', 2).arg(mouseY, 0, 'f', 2); }
	m_infoLabel->setText(text);
	m_infoLabel->adjustSize();
	m_infoLabel->move(mapFromGlobal(globalPos) + QPoint(20, 5));
	m_infoLabel->setVisible(true);
	ui.canvas_view_main->viewport()->update();  // 强制 GL 视口重绘，清除叠加标签移动后的边缘残留
}

bool ImageCanvasView::loadImageFromPath(const QString& path)
{
	QImage loadImg(path);
	if (loadImg.isNull()) return false;
	QPixmap pixmap = QPixmap::fromImage(loadImg);
	m_pixmapItem->setPixmap(pixmap);
	// 图像源身份（持久）：文件源，记录出处路径与展示名（顶层数据结构的落地入口）。
	m_imageSource.id   = QStringLiteral("image0");   /*  单图源固定 id；多源时上层生成 */
	m_imageSource.name = QFileInfo(path).completeBaseName();
	m_imageSource.type = ImageSourceType::File;
	m_imageSource.path = path;
	// 【销毁点】换图使旧图检测结果失效，清空数据层 + 渲染层。
	// 覆盖 slotLoadImage（换图）与 slotLoadRecipe（加载方案）两个场景。
	m_detectModel.clear();
	clearDetectResultOverlay();
	// 扩展 sceneRect，在图片外留出充足空间，避免中键拖拽时被限制在图片边界内
	const qreal pad = 10000.0;
	m_scene->setSceneRect(pixmap.rect().adjusted(-pad, -pad, pad, pad));
	m_toolbar->updateResolution(loadImg.width(), loadImg.height());
	updateCenterCross();
	slotZoomFit();
	return true;
}

void ImageCanvasView::slotLoadImage()
{
	QString filePath = QFileDialog::getOpenFileName(this,
		QStringLiteral("选择图片"), "", QStringLiteral("图片文件(*.png *.jpg *.bmp *.jpeg)"));
	if (filePath.isEmpty()) return;
	if (!loadImageFromPath(filePath))
	{
		QMessageBox::warning(this, QStringLiteral("图片加载失败"), QStringLiteral("无法加载图片"));
		return;
	}
	m_imagePath = filePath;
}

// ===== 方案持久化（JSON） =====

void ImageCanvasView::slotSaveRecipe()
{
	if (m_imagePath.isEmpty())
	{
		QMessageBox::warning(this, QStringLiteral("保存方案"), QStringLiteral("请先加载图片"));
		return;
	}
	if (m_shapes.isEmpty())
	{
		QMessageBox::information(this, QStringLiteral("保存方案"), QStringLiteral("当前没有可保存的图形"));
		return;
	}

	QString defaultPath = QFileInfo(m_imagePath).absolutePath() + "/" +
		QFileInfo(m_imagePath).completeBaseName() + ".json";
	QString filePath = QFileDialog::getSaveFileName(this, QStringLiteral("保存方案"),
		defaultPath, QStringLiteral("方案文件(*.json)"));
	if (filePath.isEmpty()) return;

	QJsonArray shapeArr;
	for (const DrawShapeItem& s : m_shapes)
		shapeArr.append(RecipeIO::shapeToJson(s));

	QJsonObject sizeObj;
	sizeObj["width"] = m_pixmapItem->pixmap().width();
	sizeObj["height"] = m_pixmapItem->pixmap().height();

	QJsonObject root;
	root["version"] = 1;
	root["imagePath"] = m_imagePath;
	root["imageSize"] = sizeObj;
	root["shapes"] = shapeArr;

	QFile f(filePath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		QMessageBox::warning(this, QStringLiteral("保存方案"), QStringLiteral("无法写入文件：%1").arg(filePath));
		return;
	}
	f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	f.close();
	QMessageBox::information(this, QStringLiteral("保存方案"),
		QStringLiteral("已保存 %1 个几何方案").arg(m_shapes.size()));
}

void ImageCanvasView::slotLoadRecipe()
{
	QString filePath = QFileDialog::getOpenFileName(this,
		QStringLiteral("加载方案"), "", QStringLiteral("方案文件(*.json)"));
	if (filePath.isEmpty()) return;

	QFile f(filePath);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QMessageBox::warning(this, QStringLiteral("加载方案"), QStringLiteral("无法打开文件：%1").arg(filePath));
		return;
	}
	QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	f.close();
	if (!doc.isObject())
	{
		QMessageBox::warning(this, QStringLiteral("加载方案"), QStringLiteral("方案文件格式无效"));
		return;
	}
	QJsonObject root = doc.object();

	// 方案 schema 版本：旧文件可能缺失 version，默认按 v1 处理（向后兼容）
	const int version = root["version"].toInt(1);
	Q_UNUSED(version);   /*  当前仅 v1；未来 schema 演进时在此按 version 分派  */

	// 关联图片：优先按记录路径加载，缺失时回退到文件同目录
	QString imagePath = root["imagePath"].toString();
	if (!imagePath.isEmpty() && !QFileInfo::exists(imagePath))
	{
		QString fallback = QFileInfo(filePath).absolutePath() + "/" + QFileInfo(imagePath).fileName();
		if (QFileInfo::exists(fallback)) imagePath = fallback;
	}
	if (imagePath.isEmpty() || !loadImageFromPath(imagePath))
	{
		QMessageBox::warning(this, QStringLiteral("加载方案"),
			QStringLiteral("方案关联的图片不存在或无法加载：%1").arg(imagePath));
		return;
	}
	m_imagePath = imagePath;

	// 重建方案数据
	clearAllShapes();
	const QJsonArray shapeArr = root["shapes"].toArray();
	for (const auto& v : shapeArr)
	{
		DrawShapeItem s(Shape_Rect);
		if (RecipeIO::shapeFromJson(v.toObject(), s))
			m_shapes.append(std::move(s));
	}

	// 显示当前下拉框对应的图形（无则显示第一个）
	int idx = ui.draw_cbox_shape_type->currentIndex();
	int toShow = -1;
	if (idx > 0) toShow = findShapeByType(static_cast<DrawShapeType>(idx - 1));
	if (toShow < 0 && !m_shapes.isEmpty()) toShow = 0;
	if (toShow >= 0) { m_activeShapeIndex = toShow; rebuildShapeOnScene(toShow); }

	QMessageBox::information(this, QStringLiteral("加载方案"),
		QStringLiteral("已加载 %1 个几何方案").arg(m_shapes.size()));
}

// ===== Combo box 切换 =====

void ImageCanvasView::slot_draw_shape_changed(int index)
{
	if (index <= 0)
	{
		if (m_mode == Mode_Draw) stopDraw();
		clearSceneShape();
		m_isShapeHovered = false;
		m_activeShapeIndex = -1;
		ui.canvas_view_main->setCursor(Qt::ArrowCursor);
		return;
	}

	int shapeIdx = index - 1;
	DrawShapeType type = static_cast<DrawShapeType>(shapeIdx);
	m_currentShape = type;

	// 先清掉 scene 上旧的
	clearSceneShape();
	m_isShapeHovered = false;
	m_activeShapeIndex = -1;

	int existing = findShapeByType(type);
	if (existing >= 0)
	{
		// 已有数据 → 重绘
		m_activeShapeIndex = existing;
		rebuildShapeOnScene(existing);
		ui.canvas_view_main->setCursor(Qt::ArrowCursor);
	}
	else
	{
		// 无数据 → 进入绘制模式
		startDraw(type);
	}
}

void ImageCanvasView::slotResetShape()
{
	int index = ui.draw_cbox_shape_type->currentIndex();
	if (index <= 0) return;
	DrawShapeType type = static_cast<DrawShapeType>(index - 1);

	int shapeIdx = findShapeByType(type);
	if (shapeIdx < 0) return;

	// 清除 scene + 从列表删除
	clearSceneShape();
	m_shapes.removeAt(shapeIdx);
	m_isShapeHovered = false;
	m_activeShapeIndex = -1;

	// 进入绘制模式
	startDraw(type);
}

// ===== Scene 清理 & 重绘 =====

void ImageCanvasView::clearSceneShape()
{
	if (m_shapeItem) { m_scene->removeItem(m_shapeItem); delete m_shapeItem; m_shapeItem = nullptr; }
	if (m_activeHandleSet) m_handleHelper->clearHandles(*m_activeHandleSet);
}

void ImageCanvasView::clearAllShapes()
{
	clearSceneShape();
	m_shapes.clear();
	m_activeShapeIndex = -1;
	m_isShapeHovered = false;
}

void ImageCanvasView::rebuildShapeOnScene(int shapeIndex)
{
	if (shapeIndex < 0 || shapeIndex >= m_shapes.size()) return;
	const DrawShapeItem& shape = m_shapes.at(shapeIndex);
	m_shapeItem = m_painter->buildItem(shape);
	if (m_shapeItem) { m_scene->addItem(m_shapeItem); m_shapeItem->setAcceptHoverEvents(true); }
	m_painter->applyStyle(m_shapeItem, m_colorSelected, m_penWidth, false);
	if (m_activeHandleSet) m_handleHelper->rebuildHandles(shape, *m_activeHandleSet);
	if (!m_showControlPoints && m_activeHandleSet)
		m_handleHelper->setHandlesVisible(*m_activeHandleSet, false);
}

// ===== 样式 =====
// applyStyle 已迁入 ShapePainter::applyStyle，通过 m_painter 调用

// ===== 绘制模式浮层 =====

void ImageCanvasView::showDrawModeOverlay()
{
	QWidget* parent = ui.group_draw_opt;
	if (!m_drawModeOverlay)
	{
		m_drawModeOverlay = new QWidget(parent);
		m_drawModeOverlay->setObjectName("drawModeOverlay");
		m_drawModeOverlay->setStyleSheet(
			"QWidget#drawModeOverlay { background-color: rgba(110, 110, 110, 180); border-radius: 6px; }");
		m_drawModeOverlay->installEventFilter(this);

		QVBoxLayout* outerLayout = new QVBoxLayout(m_drawModeOverlay);
		outerLayout->setAlignment(Qt::AlignCenter);

		QLabel* hintLabel = new QLabel(QStringLiteral("绘制模式中"), m_drawModeOverlay);
		hintLabel->setStyleSheet("color: white; font-size: 14px; font-weight: bold; background: transparent; border: none;");
		hintLabel->setAlignment(Qt::AlignCenter);

		m_btnCancelDraw = new QPushButton(QStringLiteral("取消绘制"), m_drawModeOverlay);
		m_btnCancelDraw->setFixedSize(100, 32);
		m_btnCancelDraw->setStyleSheet(
			"QPushButton { background-color: #e74c3c; color: white; border: none; border-radius: 4px; font-size: 13px; }"
			"QPushButton:hover { background-color: #c0392b; }"
			"QPushButton:pressed { background-color: #a93226; }");
		m_btnCancelDraw->setFocusPolicy(Qt::NoFocus);

		QHBoxLayout* btnLayout = new QHBoxLayout();
		btnLayout->setAlignment(Qt::AlignCenter);
		btnLayout->addWidget(m_btnCancelDraw);

		outerLayout->addWidget(hintLabel);
		outerLayout->addSpacing(10);
		outerLayout->addLayout(btnLayout);

		connect(m_btnCancelDraw, &QPushButton::clicked, this, [this]() {
			stopDraw();
			ui.draw_cbox_shape_type->blockSignals(true);
			ui.draw_cbox_shape_type->setCurrentIndex(0);
			ui.draw_cbox_shape_type->blockSignals(false);
		});
	}
	m_drawModeOverlay->setGeometry(parent->contentsRect());
	m_drawModeOverlay->show();
	m_drawModeOverlay->raise();
}

void ImageCanvasView::hideDrawModeOverlay()
{
	if (m_drawModeOverlay) m_drawModeOverlay->hide();
}

// ===== 参数面板 =====

void ImageCanvasView::slotOpenParamPanel() { showParamPanel(); }

void ImageCanvasView::showParamPanel() {
	int index = ui.draw_cbox_shape_type->currentIndex();
	if (index <= 0) return;
	DrawShapeType type = static_cast<DrawShapeType>(index - 1);
	int existingIdx = findShapeByType(type);

	if (!m_paramPanel) {
		m_paramPanel = new ParamPanelWidget(ui.group_draw_opt);
		m_paramPanel->installEventFilter(this);
		connect(m_paramPanel, &ParamPanelWidget::confirmed, this, &ImageCanvasView::applyParamAndRedraw);
		connect(m_paramPanel, &ParamPanelWidget::cancelled,  this, &ImageCanvasView::hideParamPanel);
		connect(m_paramPanel, &ParamPanelWidget::valueChanged, this, [this](){
			int idx=ui.draw_cbox_shape_type->currentIndex(); if(idx<=0)return;
			int si=findShapeByType(static_cast<DrawShapeType>(idx-1));
			if(si>=0) {
				DrawShapeItem& s=m_shapes[si];
				QList<ParamField> fields;
				if (s.type == Shape_Polygon) {
					for (int i = 0; i < s.pts.size(); ++i) {
						int vi = i;
						fields.append({QString(), -999999, 999999, [vi](const DrawShapeItem& a){return a.pts[vi].x();}, [vi](DrawShapeItem& a,double v){a.pts[vi].rx()=v;}});
						fields.append({QString(), -999999, 999999, [vi](const DrawShapeItem& a){return a.pts[vi].y();}, [vi](DrawShapeItem& a,double v){a.pts[vi].ry()=v;}});
					}
				} else {
					fields = ParamFieldFactory::buildFields(s.type);
				}
				m_paramPanel->applyValues(fields, s);
				if (m_shapeItem) { m_scene->removeItem(m_shapeItem); delete m_shapeItem; m_shapeItem = nullptr; }
				if (m_activeHandleSet) m_handleHelper->clearHandles(*m_activeHandleSet);
				rebuildShapeOnScene(si);
			}
		});
	}

	DrawShapeItem* existing = (existingIdx >= 0) ? &m_shapes[existingIdx] : nullptr;
	QList<ParamField> fields;
	if (type == Shape_Polygon && existing) {
		for (int i = 0; i < existing->pts.size(); ++i) {
			int idx = i;
			fields.append({QString(), -999999, 999999,
				[idx](const DrawShapeItem& s){return s.pts[idx].x();},
				[idx](DrawShapeItem& s,double v){s.pts[idx].rx()=v;}});
			fields.append({QString(), -999999, 999999,
				[idx](const DrawShapeItem& s){return s.pts[idx].y();},
				[idx](DrawShapeItem& s,double v){s.pts[idx].ry()=v;}});
		}
	} else {
		fields = ParamFieldFactory::buildFields(type);
	}

	m_paramPanel->buildUI(fields, existing, type == Shape_Polygon);

	QRect parentRect = ui.group_draw_opt->contentsRect();
	if (parentRect.height() > 400) parentRect.setHeight(400);
	m_paramPanel->setGeometry(parentRect);
	m_paramPanel->show();
	m_paramPanel->raise();
}

void ImageCanvasView::hideParamPanel() { if (m_paramPanel) m_paramPanel->hide(); }

void ImageCanvasView::syncParamPanel(int shapeIndex) {
	if (!m_paramPanel || shapeIndex < 0 || shapeIndex >= m_shapes.size()) return;
	const DrawShapeItem& shape = m_shapes.at(shapeIndex);
	QList<ParamField> fields;
	if (shape.type == Shape_Polygon) {
		for (int i = 0; i < shape.pts.size(); ++i) {
			int vi = i;
			fields.append({QString(), -999999, 999999, [vi](const DrawShapeItem& a){return a.pts[vi].x();}, [vi](DrawShapeItem& a,double v){a.pts[vi].rx()=v;}});
			fields.append({QString(), -999999, 999999, [vi](const DrawShapeItem& a){return a.pts[vi].y();}, [vi](DrawShapeItem& a,double v){a.pts[vi].ry()=v;}});
		}
	} else {
		fields = ParamFieldFactory::buildFields(shape.type);
	}
	m_paramPanel->syncValues(fields, shape);
}

void ImageCanvasView::applyParamAndRedraw() {
	int index = ui.draw_cbox_shape_type->currentIndex();
	if (index <= 0) return;
	DrawShapeType type = static_cast<DrawShapeType>(index - 1);
	int shapeIdx = findShapeByType(type);
	if (shapeIdx < 0) {
		m_shapes.append(DrawShapeItem(type));
		shapeIdx = m_shapes.size() - 1;
	}

	DrawShapeItem& shape = m_shapes[shapeIdx];
	QList<ParamField> fields;
	if (type == Shape_Polygon) {
		for (int i = 0; i < shape.pts.size(); ++i) {
			int idx = i;
			fields.append({QString(), -999999, 999999, [idx](const DrawShapeItem& s){return s.pts[idx].x();}, [idx](DrawShapeItem& s,double v){s.pts[idx].rx()=v;}});
			fields.append({QString(), -999999, 999999, [idx](const DrawShapeItem& s){return s.pts[idx].y();}, [idx](DrawShapeItem& s,double v){s.pts[idx].ry()=v;}});
		}
	} else {
		fields = ParamFieldFactory::buildFields(type);
	}
	m_paramPanel->applyValues(fields, shape);

	clearSceneShape();
	m_activeShapeIndex = shapeIdx;
	rebuildShapeOnScene(shapeIdx);
	hideParamPanel();
}

// ===== 绘制流程 =====

void ImageCanvasView::startDraw(DrawShapeType type)
{
	m_currentShape = type;
	m_mode = Mode_Draw;
	m_drawStep = 0;
	if (m_ghostRect) { m_scene->removeItem(m_ghostRect); delete m_ghostRect; m_ghostRect = nullptr; }
	if (m_ghostEllipse) { m_scene->removeItem(m_ghostEllipse); delete m_ghostEllipse; m_ghostEllipse = nullptr; }
	if (m_ghostEllipse2) { m_scene->removeItem(m_ghostEllipse2); delete m_ghostEllipse2; m_ghostEllipse2 = nullptr; }
	if (m_ghostArcPath) { m_scene->removeItem(m_ghostArcPath); delete m_ghostArcPath; m_ghostArcPath = nullptr; }
	if (m_ghostCircumRect) { m_scene->removeItem(m_ghostCircumRect); delete m_ghostCircumRect; m_ghostCircumRect = nullptr; }
	if (m_circleMarker1) { m_scene->removeItem(m_circleMarker1); delete m_circleMarker1; m_circleMarker1 = nullptr; }
	if (m_circleMarker2) { m_scene->removeItem(m_circleMarker2); delete m_circleMarker2; m_circleMarker2 = nullptr; }
	showDrawModeOverlay();
	ui.canvas_view_main->setCursor(Qt::CrossCursor);
}

void ImageCanvasView::stopDraw()
{
	if (m_ghostRect) { m_scene->removeItem(m_ghostRect); delete m_ghostRect; m_ghostRect = nullptr; }
	if (m_ghostEllipse) { m_scene->removeItem(m_ghostEllipse); delete m_ghostEllipse; m_ghostEllipse = nullptr; }
	if (m_ghostEllipse2) { m_scene->removeItem(m_ghostEllipse2); delete m_ghostEllipse2; m_ghostEllipse2 = nullptr; }
	if (m_ghostArcPath) { m_scene->removeItem(m_ghostArcPath); delete m_ghostArcPath; m_ghostArcPath = nullptr; }
	if (m_ghostCircumRect) { m_scene->removeItem(m_ghostCircumRect); delete m_ghostCircumRect; m_ghostCircumRect = nullptr; }
	if (m_circleMarker1) { m_scene->removeItem(m_circleMarker1); delete m_circleMarker1; m_circleMarker1 = nullptr; }
	if (m_circleMarker2) { m_scene->removeItem(m_circleMarker2); delete m_circleMarker2; m_circleMarker2 = nullptr; }
	if (m_ghostPolyLine) { m_scene->removeItem(m_ghostPolyLine); delete m_ghostPolyLine; m_ghostPolyLine = nullptr; }
	for (auto* mk : m_circleMarkers) { m_scene->removeItem(mk); delete mk; }
	m_circleMarkers.clear();
	m_tempPolyPts.clear();
	m_mode = Mode_None;
	m_drawStep = 0;
	hideDrawModeOverlay();
	ui.canvas_view_main->setCursor(Qt::ArrowCursor);
}

void ImageCanvasView::updateGhostRect(const QPointF& scenePos)
{
	if (!m_ghostRect) return;
	double x1 = std::min(m_anchorPoint.x(), scenePos.x());
	double y1 = std::min(m_anchorPoint.y(), scenePos.y());
	double x2 = std::max(m_anchorPoint.x(), scenePos.x());
	double y2 = std::max(m_anchorPoint.y(), scenePos.y());
	m_ghostRect->setRect(QRectF(QPointF(x1, y1), QPointF(x2, y2)));
}

void ImageCanvasView::commitShapeGeneric(DrawShapeType type)
{
	switch (type)
	{
	case Shape_Rect:       commitRect();       break;
	case Shape_RotateRect: commitRotatedRect();break;
	case Shape_Ellipse:    commitEllipse();    break;
	default: break;
	}
}

// ===== 圆形提交（三点画圆） =====
void ImageCanvasView::commitCircle()
{
	double x1=m_circlePt1.x(), y1=m_circlePt1.y();
	double x2=m_circlePt2.x(), y2=m_circlePt2.y();
	if (!m_ghostEllipse) return;
	QRectF r = m_ghostEllipse->rect();
	double cx = r.center().x(), cy = r.center().y(), radius = r.width()/2.0;
	if (radius < 1.5) return;

	int shapeIdx = findShapeByType(Shape_Circle);
	if (shapeIdx < 0) { m_shapes.append(DrawShapeItem(Shape_Circle)); shapeIdx = m_shapes.size() - 1; }
	DrawShapeItem& shape = m_shapes[shapeIdx];
	shape.cx = cx; shape.cy = cy; shape.r = radius;

	clearSceneShape();
	m_activeShapeIndex = shapeIdx;
	rebuildShapeOnScene(shapeIdx);
}

// ===== 圆环提交（四点构造） =====
void ImageCanvasView::commitRing()
{
	// 第一个圆的半径存于 m_dragStartRect.w/2
	double cx = m_dragStartRect.cx, cy = m_dragStartRect.cy;
	double r1 = m_dragStartRect.w / 2.0;
	double r2 = 0;
	if (m_ghostEllipse2)
	{
		QRectF r = m_ghostEllipse2->rect();
		r2 = r.width() / 2.0;
	}
	if (r1 < 1.5 || r2 < 1.5) return;

	int shapeIdx = findShapeByType(Shape_Ring);
	if (shapeIdx < 0) { m_shapes.append(DrawShapeItem(Shape_Ring)); shapeIdx = m_shapes.size() - 1; }
	DrawShapeItem& shape = m_shapes[shapeIdx];
	shape.cx = cx; shape.cy = cy; shape.r = r1; shape.r2 = r2;

	clearSceneShape();
	m_activeShapeIndex = shapeIdx;
	rebuildShapeOnScene(shapeIdx);
}

// ===== 扇环提交 =====
void ImageCanvasView::commitArc()
{
	double cx=m_dragStartRect.cx,cy=m_dragStartRect.cy,r1=m_dragStartRect.w,r2=m_dragStartRect.h;
	r2=std::max(r2,2.0);if(r1<2.0||r2<2.0)return;
	double aA=m_arcStartAngle,span=m_arcEndAngle;
	double rOuter=std::max(r1,r2),rInner=std::min(r1,r2);
	int shapeIdx=findShapeByType(Shape_Arc);
	if(shapeIdx<0){m_shapes.append(DrawShapeItem(Shape_Arc));shapeIdx=m_shapes.size()-1;}
	DrawShapeItem& shape=m_shapes[shapeIdx];
	shape.cx=cx;shape.cy=cy;
	shape.r=rOuter;shape.r2=rInner;
	shape.startAngle=aA;shape.endAngle=aA+span;shape.span=span;
	clearSceneShape();m_activeShapeIndex=shapeIdx;rebuildShapeOnScene(shapeIdx);
}

// ===== 多边形提交 =====
void ImageCanvasView::commitPolygon()
{
	if(m_tempPolyPts.size()<3)return;
	int shapeIdx=findShapeByType(Shape_Polygon);
	if(shapeIdx<0){m_shapes.append(DrawShapeItem(Shape_Polygon));shapeIdx=m_shapes.size()-1;}
	DrawShapeItem& shape=m_shapes[shapeIdx];
	shape.pts=m_tempPolyPts;
	// 清理绘制标记
	for(auto* mk:m_circleMarkers){m_scene->removeItem(mk);delete mk;}
	m_circleMarkers.clear();
	if(m_ghostPolyLine){m_scene->removeItem(m_ghostPolyLine);delete m_ghostPolyLine;m_ghostPolyLine=nullptr;}
	if(m_ghostArcPath){m_scene->removeItem(m_ghostArcPath);delete m_ghostArcPath;m_ghostArcPath=nullptr;}
	m_tempPolyPts.clear();
	hideDrawModeOverlay();m_mode=Mode_None;m_drawStep=0;
	ui.canvas_view_main->setCursor(Qt::ArrowCursor);
	clearSceneShape();m_activeShapeIndex=shapeIdx;rebuildShapeOnScene(shapeIdx);
}





void ImageCanvasView::commitRect()
{
	if (!m_ghostRect) return;
	QRectF gr = m_ghostRect->rect();
	double cx = gr.center().x();
	double cy = gr.center().y();
	double w = gr.width();
	double h = gr.height();
	if (w < 3 || h < 3) return;

	int shapeIdx = findShapeByType(Shape_Rect);
	if (shapeIdx < 0) { m_shapes.append(DrawShapeItem(Shape_Rect)); shapeIdx = m_shapes.size() - 1; }
	DrawShapeItem& shape = m_shapes[shapeIdx];
	shape.cx = cx; shape.cy = cy; shape.w = w; shape.h = h;

	clearSceneShape();
	m_activeShapeIndex = shapeIdx;
	rebuildShapeOnScene(shapeIdx);
}

void ImageCanvasView::commitRotatedRect()
{
	if (!m_ghostRect) return;
	QRectF gr = m_ghostRect->rect();
	double cx = gr.center().x();
	double cy = gr.center().y();
	double w  = gr.width();
	double h  = gr.height();
	if (w < 3 || h < 3) return;

	int shapeIdx = findShapeByType(Shape_RotateRect);
	if (shapeIdx < 0) { m_shapes.append(DrawShapeItem(Shape_RotateRect)); shapeIdx = m_shapes.size() - 1; }
	DrawShapeItem& shape = m_shapes[shapeIdx];
	shape.cx = cx; shape.cy = cy; shape.w = w; shape.h = h; shape.angle = 0.0;

	clearSceneShape();
	m_activeShapeIndex = shapeIdx;
	rebuildShapeOnScene(shapeIdx);
}

// ===== 椭圆提交 =====
void ImageCanvasView::commitEllipse()
{
	if (!m_ghostRect) return;
	QRectF gr = m_ghostRect->rect();
	double cx = gr.center().x(), cy = gr.center().y();
	double r1 = gr.width()/2.0, r2 = gr.height()/2.0;
	if (r1 < 1.5 || r2 < 1.5) return;

	int shapeIdx = findShapeByType(Shape_Ellipse);
	if (shapeIdx < 0) { m_shapes.append(DrawShapeItem(Shape_Ellipse)); shapeIdx = m_shapes.size() - 1; }
	DrawShapeItem& shape = m_shapes[shapeIdx];
	shape.cx = cx; shape.cy = cy; shape.r = r1; shape.r2 = r2; shape.angle = 0.0;

	clearSceneShape();
	m_activeShapeIndex = shapeIdx;
	rebuildShapeOnScene(shapeIdx);
}

// ===== 形状查询 =====

/*
 *  【过渡态】当前数据模型：每种图形类型全局至多一个实例（单实例模型）。
 *  该约束内嵌于本函数——它返回"首个匹配 type 的对象"并复用，而非新建。
 *
 *  演进方向（多实例化，未来"多个检测目标"时触发）：
 *    1. 用 DrawShapeItem::id 作为唯一标识，新增 findShapeById(id) 定位；
 *    2. commit* 系列改为"总是 new + 追加到 m_shapes"，而非 findShapeByType 复用；
 *    3. 本函数届时废弃或改语义为"返回全部同 type 实例"。
 */
int ImageCanvasView::findShapeByType(DrawShapeType type)
{
	for (int i = 0; i < m_shapes.size(); ++i)
		if (m_shapes.at(i).type == type) return i;
	return -1;
}

// ===== 图形构建 =====

// buildShapeItem 已迁入 ShapePainter::buildItem


// ===== 控制点 & 中心十字 =====
// (已迁移至 ShapeHandleHelper)

QGraphicsEllipseItem* ImageCanvasView::handleAt(const QPointF& scenePos) const
{
	if (!m_activeHandleSet) return nullptr;
	return m_handleHelper->handleAt(*m_activeHandleSet, scenePos);
}

// ===== Handle 拖拽 =====



void ImageCanvasView::applyShapeHover(bool hover)
{
	if (m_shapeItem)
		m_painter->applyStyle(m_shapeItem, m_colorSelected, m_penWidth, hover);
}

/*  拖拽编辑后统一刷新控制点位置与参数面板（等价于原 update*FromHandle 末尾副作用） */
void ImageCanvasView::refreshActiveShape()
{
	if (m_dragShapeIndex < 0 || m_dragShapeIndex >= m_shapes.size()) return;
	DrawShapeItem& ds = m_shapes[m_dragShapeIndex];
	m_handleHelper->updatePositions(ds, *m_activeHandleSet, m_shapeItem);
	syncParamPanel(m_dragShapeIndex);
}

/* ===== 缺陷检测：运行算法，结果存入宿主并只读上屏 ===== */

/*  【检测结果渲染层唯一销毁入口】
 *   removeItem(item) 与 delete item 必须成对：
 *     只 delete 不 removeItem → scene 持悬垂指针；
 *     只 removeItem 不 delete → 内存泄漏。
 *   所有"检测结果失效"的场景（换图 / 加载方案 / 重跑 / 析构）都必须经由本函数。 */
void ImageCanvasView::clearDetectResultOverlay()
{
	for (auto* item : m_detectResultItems) { m_scene->removeItem(item); delete item; }
	m_detectResultItems.clear();
	for (auto* label : m_detectResultLabels) { m_scene->removeItem(label); delete label; }
	m_detectResultLabels.clear();
}

/*  将 m_detectModel 中的全部检测框渲染到叠加层（由数据驱动，而非手填 item） */
void ImageCanvasView::renderDetectOverlay()
{
	clearDetectResultOverlay();

	QPen boxPen(QColor(255, 60, 60));
	boxPen.setWidth(2);
	boxPen.setCosmetic(true);

	for (const DetectionBox& box : m_detectModel.allDetections())
	{
		QRectF r(box.cx - box.w / 2.0, box.cy - box.h / 2.0, box.w, box.h);
		auto* rectItem = m_scene->addRect(r, boxPen, QBrush(Qt::NoBrush));
		rectItem->setZValue(85);
		rectItem->setFlag(QGraphicsItem::ItemIgnoresTransformations, false);
		m_detectResultItems.append(rectItem);

		auto* labelItem = m_scene->addSimpleText(
			QStringLiteral("%1 %.2f").arg(box.label).arg(box.confidence));
		labelItem->setPos(r.left(), r.top() - 18);
		labelItem->setBrush(QBrush(QColor(255, 60, 60)));
		labelItem->setZValue(86);
		m_detectResultLabels.append(labelItem);
	}
}

void ImageCanvasView::slotRunDetect()
{
	// 校验：必须有已加载图像（默认像素图为 1280x960 占位，也允许检测）
	const QPixmap pm = m_pixmapItem->pixmap();

	// QPixmap -> QImage -> cv::Mat，喂给真实缺陷检测器
	const QImage qi = pm.toImage().convertToFormat(QImage::Format_RGB888);
	const cv::Mat img = CvImageConverter::toCvMat(qi);

	// ROI 裁剪：以当前选中 shape 作为检测区域（无选中/ROI 无效则回退整图）
	cv::Mat sub;
	QPoint roiOrigin(0, 0);   /*  ROI 子图相对整图的原点，用于结果坐标回贴 */
	if (m_activeShapeIndex >= 0)
	{
		const DrawShapeItem& active = m_shapes.at(m_activeShapeIndex);
		QRect roi;
		if (ShapeGeometry::cropRect(active, img.cols, img.rows, roi))
		{
			// clone() 深拷贝：与 img 完全隔离，检测器即便写输入也不波及原图
			sub = img(cv::Rect(roi.x(), roi.y(), roi.width(), roi.height())).clone();
			roiOrigin = QPoint(roi.x(), roi.y());
		}
		else
		{
			sub = img;
		}
	}
	else
	{
		sub = img;
	}

	// 灰度缺陷检测参数：当前使用默认值；TODO: 后续从方案/检测项(itemList)读取参数
	GrayDefectDetector::GrayDetectParams params;
	AlgorithmResult result = GrayDefectDetector::detect(sub, params);

	// 坐标回贴：子图局部坐标 + ROI 原点 = 整图坐标
	for (DetectionBox& box : result.detections)
	{
		box.cx += roiOrigin.x();
		box.cy += roiOrigin.y();
	}

	// 结果存入宿主（数据层），叠层渲染由 model 驱动
	m_detectModel.clear();          // 本次运行视为一份新结果，替换旧结果
	m_detectModel.addResult(result);
	renderDetectOverlay();

	// 反馈
	ui.info_lab_resolution->setToolTip(QStringLiteral("检测到 %1 个目标").arg(result.detections.size()));
	QMessageBox::information(this, QStringLiteral("运行检测"),
		QStringLiteral("缺陷检测完成：%1 个缺陷区域").arg(result.detections.size()));
}

void ImageCanvasView::clearLocateResultOverlay()
{
	for (auto* item : m_locateResultItems) { m_scene->removeItem(item); delete item; }
	m_locateResultItems.clear();
}

void ImageCanvasView::slotRunLocate()
{
	// 临时验证入口：喂图 → 定位 → 画出 Pose2D（中心十字 + 角度方向线）
	const QPixmap pm = m_pixmapItem->pixmap();
	const QImage qi = pm.toImage().convertToFormat(QImage::Format_RGB888);
	const cv::Mat img = CvImageConverter::toCvMat(qi);

	AlgorithmResult result = PoseLocator::locate(img);

	clearLocateResultOverlay();

	if (result.poses.isEmpty())
	{
		QMessageBox::information(this, QStringLiteral("定位"),
			QStringLiteral("未找到有效目标（poses 为空）"));
		return;
	}

	const Pose2D& pose = result.poses.first().pose;

	// 中心十字
	QPen pen(QColor(0, 170, 90));
	pen.setWidth(2);
	pen.setCosmetic(true);
	const double cs = 12.0;
	QLineF lineH(pose.tx - cs, pose.ty, pose.tx + cs, pose.ty);
	QLineF lineV(pose.tx, pose.ty - cs, pose.tx, pose.ty + cs);
	auto* h = m_scene->addLine(lineH, pen);
	auto* v = m_scene->addLine(lineV, pen);
	h->setZValue(87); v->setZValue(87);
	m_locateResultItems.append(h);
	m_locateResultItems.append(v);

	// 角度方向线（长度随 score 放大，指向 minAreaRect 旋转角方向）
	const double len = 60.0;
	const QPointF dir = QPointF(pose.tx + len * qCos(pose.angle), pose.ty + len * qSin(pose.angle));
	auto* dLine = m_scene->addLine(QLineF(QPointF(pose.tx, pose.ty), dir), pen);
	dLine->setZValue(87);
	m_locateResultItems.append(dLine);

	// 标签
	auto* label = m_scene->addSimpleText(
		QStringLiteral("blob (%.1f, %.1f) %%.2f° score=%.2f")
			.arg(pose.tx).arg(pose.ty)
			.arg(qRadiansToDegrees(pose.angle))
			.arg(pose.score));
	label->setPos(pose.tx + 8, pose.ty + 8);
	label->setBrush(QBrush(QColor(0, 170, 90)));
	label->setZValue(88);
	m_locateResultItems.append(label);

	QMessageBox::information(this, QStringLiteral("定位"),
		QStringLiteral("定位完成：中心 (%.1f, %.1f)，角度 %.2f°，score %.2f")
			.arg(pose.tx).arg(pose.ty)
			.arg(qRadiansToDegrees(pose.angle))
			.arg(pose.score));
}

void ImageCanvasView::clearCaliperResultOverlay()
{
	for (auto* item : m_caliperResultItems) { m_scene->removeItem(item); delete item; }
	m_caliperResultItems.clear();
}

void ImageCanvasView::slotRunCaliper()
{
	// 临时验证入口：以图像中部构造一个竖直卡尺框（长轴竖直、搜索方向水平），
	// 检测图中的竖直线边缘，画出拟合直线 + 边缘散点。
	const QPixmap pm = m_pixmapItem->pixmap();
	const QImage qi = pm.toImage().convertToFormat(QImage::Format_RGB888);
	const cv::Mat img = CvImageConverter::toCvMat(qi);

	const double cx = img.cols / 2.0;
	// 卡尺框长轴：竖直方向，从 (cx, cy0) 到 (cx, cy1)，覆盖图像中部 80%
	const double cy0 = img.rows * 0.1;
	const double cy1 = img.rows * 0.9;

	CaliperDetector::CaliperParams params;   /*  默认参数 */
	AlgorithmResult result = CaliperDetector::findLine(img,
		cv::Point2f(static_cast<float>(cx), static_cast<float>(cy0)),
		cv::Point2f(static_cast<float>(cx), static_cast<float>(cy1)),
		params);

	clearCaliperResultOverlay();

	// 画卡尺框（供参考）
	QPen boxPen(QColor(0, 120, 255));
	boxPen.setWidth(1);
	boxPen.setCosmetic(true);
	boxPen.setStyle(Qt::DashLine);

	// 卡尺框 = 长轴两侧 ±projectionLen 的矩形
	const double halfL = params.projectionLen;
	QPolygonF caliperBox;
	caliperBox << QPointF(cx - halfL, cy0) << QPointF(cx + halfL, cy0)
	           << QPointF(cx + halfL, cy1) << QPointF(cx - halfL, cy1);
	auto* boxItem = m_scene->addPolygon(caliperBox, boxPen, QBrush(Qt::NoBrush));
	boxItem->setZValue(87);
	m_caliperResultItems.append(boxItem);

	if (result.geometry.isEmpty())
	{
		QMessageBox::information(this, QStringLiteral("卡尺"),
			QStringLiteral("未拟合到直线（geometry 为空）"));
		return;
	}

	// 画拟合直线（Geom_Segment）
	QPen linePen(QColor(255, 60, 60));
	linePen.setWidth(2);
	linePen.setCosmetic(true);

	double lineScore = 0.0;
	bool hasLine = false;
	QPolygonF edgePts;

	for (const ResultGeom& g : result.geometry)
	{
		if (g.geom.type == Geom_Segment)
		{
			auto* ln = m_scene->addLine(
				QLineF(QPointF(g.geom.cx, g.geom.cy), QPointF(g.geom.ex, g.geom.ey)), linePen);
			ln->setZValue(88);
			m_caliperResultItems.append(ln);
			lineScore = g.score;
			hasLine = true;
		}
		else if (g.geom.type == Geom_Contour)
		{
			// 边缘散点：画小十字
			QPen ptPen(QColor(0, 170, 90));
			ptPen.setWidth(1);
			ptPen.setCosmetic(true);
			const int n = g.geom.contour.size();
			for (int i = 0; i < n; ++i)
			{
				const QPointF& p = g.geom.contour.at(i);
				auto* ph = m_scene->addLine(QLineF(p.x() - 3, p.y(), p.x() + 3, p.y()), ptPen);
				auto* pv = m_scene->addLine(QLineF(p.x(), p.y() - 3, p.x(), p.y() + 3), ptPen);
				ph->setZValue(88); pv->setZValue(88);
				m_caliperResultItems.append(ph);
				m_caliperResultItems.append(pv);
			}
		}
	}

	QString msg = hasLine
		? QStringLiteral("卡尺拟合完成：直线 score=%.2f").arg(lineScore)
		: QStringLiteral("卡尺拟合完成：无直线（仅散点）");
	QMessageBox::information(this, QStringLiteral("卡尺"), msg);
}

void ImageCanvasView::setupFlowDock()
{
	// 左侧流程树 Dock：可拖拽排序的 QTreeWidget + 底部「运行流程」按钮
	m_flowTree = new QTreeWidget();
	m_flowTree->setHeaderHidden(true);
	m_flowTree->setDragDropMode(QAbstractItemView::InternalMove);   // 可拖拽排序
	m_flowTree->setSelectionMode(QAbstractItemView::SingleSelection);

	// 默认流程：定位(blob) -> 检测(灰度缺陷) 顺序两步（端到端最小数据流）。
	// 定位产出 Pose2D，检测吃 ROI 子图（当前为人工 ROI，未来由定位校正跟随）；
	// 用户可按需通过拖拽排序 / 后续的「增删工具」扩展。
	auto addTool = [this](ToolCategory cat, const QString& algo, const QString& display) {
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0, QStringLiteral("%1 · %2").arg(toolCategoryName(cat), display));
		item->setData(0, Qt::UserRole, algo);           // 算法 tag
		item->setData(0, Qt::UserRole + 1, static_cast<int>(cat)); // 大类
		item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
		m_flowTree->addTopLevelItem(item);
	};

	addTool(Category_Locate,  "blobLocator", QStringLiteral("blob 定位"));
	addTool(Category_Inspect, "grayDefect",  QStringLiteral("灰度缺陷检测"));

	m_btnRunFlow = new QPushButton(QStringLiteral("运行流程"));

	QWidget* host = new QWidget();
	QVBoxLayout* lay = new QVBoxLayout(host);
	lay->setContentsMargins(4, 4, 4, 4);
	lay->addWidget(m_flowTree, 1);
	lay->addWidget(m_btnRunFlow, 0);

	m_flowDock = new QDockWidget(QStringLiteral("流程"), this);
	m_flowDock->setObjectName("flowDock");
	m_flowDock->setWidget(host);
	m_flowDock->setMinimumWidth(260);
	addDockWidget(Qt::LeftDockWidgetArea, m_flowDock);

	connect(m_btnRunFlow, &QPushButton::clicked, this, &ImageCanvasView::slotRunFlow);
}

QList<ToolStep> ImageCanvasView::collectSteps() const
{
	QList<ToolStep> steps;
	if (!m_flowTree)
		return steps;

	const int n = m_flowTree->topLevelItemCount();
	for (int i = 0; i < n; ++i)
	{
		QTreeWidgetItem* item = m_flowTree->topLevelItem(i);
		ToolStep step;
		step.algorithm = item->data(0, Qt::UserRole).toString();
		step.category  = static_cast<ToolCategory>(item->data(0, Qt::UserRole + 1).toInt());
		step.id        = step.algorithm + QString::number(i);
		steps.append(step);
	}
	return steps;
}

void ImageCanvasView::slotRunFlow()
{
	const QPixmap pm = m_pixmapItem->pixmap();
	const QImage qi = pm.toImage().convertToFormat(QImage::Format_RGB888);

	// 图像句柄：shared_ptr<const cv::Mat>，只读引用贯穿（零拷贝，不复制像素）
	auto imageMat = std::make_shared<cv::Mat>(CvImageConverter::toCvMat(qi));

	// 只读上下文：图 + 用户几何（值拷贝：人工绘制的 ROI/基准线）+ 上游结果（初始空）
	ToolContext ctx;
	ctx.image = imageMat;
	// 图像源（顶层身份）：以 m_imageSource.id 为 key 挂入，工具可据 id 精准引用；
	// sourceId 同步填同一 id，保证 ImageData 脱离容器后仍可追溯来源。
	// 兼容字段 ctx.image 仍指向同一份数据，保证旧工具 ctx.image 语义不变。
	ctx.imageSources.insert(m_imageSource.id, ImageData::fromMat(imageMat, m_imageSource.id));
	ctx.shapes = m_shapes;   // 值语义：直接拷贝，与 m_shapes 隔离（tools 侧只读）

	QList<ToolStep> steps = collectSteps();
	if (steps.isEmpty())
	{
		QMessageBox::information(this, QStringLiteral("运行流程"), QStringLiteral("流程为空"));
		return;
	}

	// 顺序执行：每步按 name 创建工具 -> run -> 结果挂到 ctx.results（供下游吃）
	QStringList lines;
	for (const ToolStep& s : steps)
	{
		ITool* tool = createToolByName(s.algorithm);
		if (!tool)
		{
			lines << QStringLiteral("%1：未找到工具").arg(s.algorithm);
			continue;
		}

		tool->loadParams(s.params);
		ToolResult tr = tool->run(ctx);
		delete tool;

		if (!tr.ok)
		{
			lines << QStringLiteral("%1：失败（%2）").arg(s.algorithm, tr.error);
			continue;
		}

		ctx.results.insert(s.id, tr.data);   // 结果按 step.id 挂回，供下游
		const AlgorithmResult& d = tr.data;
		const int total = d.detections.size() + d.poses.size() + d.geometry.size();
		lines << QStringLiteral("%1：产出 %2 项").arg(s.algorithm).arg(total);
	}

	QMessageBox::information(this, QStringLiteral("运行流程完成"), lines.join("\n"));
}
