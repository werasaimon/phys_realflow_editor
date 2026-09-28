cmake_minimum_required(VERSION 3.21)
project(PhysRealFlowEditor LANGUAGES CXX)

# The editor: a Qt 6 viewer with control panels on top of the PhysRealFlow SDK, which comes in as
# the git submodule extern/phys_realflow (rfcore: the solvers, rfsamples: the ready-made scenes).
# The SDK itself never depends on Qt; only this program does.
#   git clone --recurse-submodules https://github.com/werasaimon/phys_realflow_editor.git
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.7.3/mingw_64
#   cmake --build build -j
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(RF_EDITOR_TESTS "Also build the SDK's rf_tests" OFF)
set(RF_BUILD_TESTS ${RF_EDITOR_TESTS} CACHE BOOL "" FORCE)
set(RF_BUILD_APP OFF CACHE BOOL "" FORCE) # the SDK's own app target (older SDK commits): this program replaces it
set(RF_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/phys_realflow" CACHE PATH
    "The PhysRealFlow SDK source tree (the submodule, or a local checkout while developing)")
add_subdirectory(${RF_SDK_DIR} phys_realflow)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Charts)
qt_standard_project_setup()

qt_add_executable(realflow WIN32
  src/main.cpp
  src/MainWindow.h      src/MainWindow.cpp src/MainWindowStart.cpp src/MainWindowControls.cpp src/MainWindowToolbar.cpp src/MainWindowPlay.cpp
  src/FirstStartHint.h  src/FirstStartHint.cpp
  src/ExamplesGallery.h src/ExamplesGallery.cpp
  src/Viewport.h        src/Viewport.cpp src/ViewportInput.cpp src/ViewportLights.cpp src/ViewportShadows.cpp src/ViewportPlay.cpp
  src/SceneMarkers.h    src/SceneMarkers.cpp
  src/NavCube.h         src/NavCube.cpp
  src/Shaders.h
  src/VolumeRenderer.cpp
  src/SimController.h   src/SimController.cpp
  src/ParamForm.h       src/ParamForm.cpp
  src/OrbitCamera.h     src/OrbitCamera.cpp
  src/ViewportTools.h   src/ViewportTools.cpp
  src/FluidSurfaceRenderer.h src/FluidSurfaceRenderer.cpp
  src/PlotPanel.h       src/PlotPanel.cpp
  src/Gizmo.h           src/Gizmo.cpp
  src/ControlScheme.h   src/ControlScheme.cpp
  src/EditView.h        src/EditView.cpp
  src/MeshNormals.h     src/MeshNormals.cpp
  src/PlayOverlay.h     src/PlayOverlay.cpp
  src/CameraPicker.h    src/CameraPicker.cpp
  src/CommandSearch.h   src/CommandSearch.cpp
  src/SceneBuilder.h    src/SceneBuilder.cpp  src/SceneBuilderPanels.cpp src/SceneBuilderColliders.cpp src/SceneBuilderSelection.cpp
  src/SceneBuilderObjects.cpp src/SceneBuilderClone.cpp src/SceneBuilderMulti.cpp src/SceneBuilderLights.cpp src/SceneBuilderKeep.cpp
  src/ManyPanels.h      src/ManyPanels.cpp
  src/ClonePopover.h    src/ClonePopover.cpp src/RuPlural.h
  src/ColliderGuides.h  src/ColliderGuides.cpp
  src/ColliderPanel.h   src/ColliderPanel.cpp
  src/ObjectInspector.h src/ObjectInspector.cpp
  src/InspectorWidgets.h src/InspectorWidgets.cpp
  src/RoleBar.h         src/RoleBar.cpp
  src/Icons.h           src/Icons.cpp
  src/SelfTest.h        src/SelfTest.cpp  src/SelfTestSupport.h src/SelfTestComponents.cpp src/SelfTestControls.cpp src/SelfTestMany.cpp src/SelfTestLights.cpp src/SelfTestShadows.cpp src/SelfTestPlay.cpp
)
target_link_libraries(realflow PRIVATE rfcore rfsamples
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Charts)
# "Открыть…" of the scene builder starts in examples/ of this source tree.
target_compile_definitions(realflow PRIVATE RF_EDITOR_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples")
if(MSVC)
  target_compile_options(realflow PRIVATE /W4 /utf-8)
else()
  target_compile_options(realflow PRIVATE -Wall -Wextra)
endif()
# Software OpenGL (Mesa llvmpipe, shipped with Qt) next to the program: with --software-gl the
# same shaders run on the CPU - for PCs without a usable GPU / driver.
if(WIN32)
  add_custom_command(TARGET realflow POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE_DIR:Qt6::Core>/opengl32sw.dll" "$<TARGET_FILE_DIR:realflow>")
endif()
