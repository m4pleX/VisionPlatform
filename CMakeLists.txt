cmake_minimum_required(VERSION 4.4)
set(CMAKE_PREFIX_PATH "D:/Qt/6.5.3/msvc2019_64")
project(VisionPlatform)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
qt_standard_project_setup()

# ==== OpenCV 4.8.1 集成（Debug/Release 分别指向不同 install 目录） ====
# 单配置生成器(Ninja)用 CMAKE_BUILD_TYPE 判断；多配置(VS)用 CMAKE_CONFIGURATION_TYPES
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(OpenCV_DIR "D:/MyCode/opencv-4.8.1/install-debug")
else()
    set(OpenCV_DIR "D:/MyCode/opencv-4.8.1/install-release")
endif()
find_package(OpenCV REQUIRED)

add_executable(VisionPlatform
        AlgorithmResult.h
        CaliperDetector.cpp
        CaliperDetector.h
        InspectionItem.h
        RecipeIO.cpp
        RecipeIO.h
        CvImageConverter.cpp
        CvImageConverter.h
        DetectionResultModel.cpp
        DetectionResultModel.h
        DrawShapeData.h
        GrayDefectDetector.cpp
        GrayDefectDetector.h
        GeomPrimitive.h
        ImageCanvasView.cpp
        ImageCanvasView.h
        ImageCanvasView.qrc
        ImageSource.h
        ImageCanvasView.ui
        main.cpp
        ParamPanelWidget.cpp
        ParamPanelWidget.h
        ParamFieldFactory.cpp
        ParamFieldFactory.h
        PoseLocator.cpp
        PoseLocator.h
        ScaleConfig.h
        ShapeGeometry.cpp
        ShapeGeometry.h
        ShapeEditor.cpp
        ShapeEditor.h
        ShapeHandleHelper.cpp
        ShapeHandleHelper.h
        ShapePainter.cpp
        ShapePainter.h
        ToolbarController.cpp
        ToolbarController.h
        ToolDefinition.h
        VisionTools.cpp
        VisionTools.h
        ITool.h)

target_link_libraries(VisionPlatform PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets ${OpenCV_LIBS})
