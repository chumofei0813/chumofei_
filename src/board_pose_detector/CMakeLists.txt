cmake_minimum_required(VERSION 3.8)
project(board_pose_detector)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(OpenCV REQUIRED)

# ============================================================================
# 海康相机节点（仅当 MVS SDK 存在时编译）
# ============================================================================
if(EXISTS /opt/MVS/include AND EXISTS /opt/MVS/lib/64)
  message(STATUS "Found Hik MVS SDK, building hik_camera_node")

  add_executable(hik_camera_node src/hik_camera_node.cpp)
  target_include_directories(hik_camera_node PRIVATE /opt/MVS/include)
  target_link_directories(hik_camera_node PRIVATE /opt/MVS/lib/64)
  ament_target_dependencies(hik_camera_node rclcpp sensor_msgs std_msgs cv_bridge)
  target_link_libraries(hik_camera_node MvCameraControl ${OpenCV_LIBS})

  install(TARGETS hik_camera_node RUNTIME DESTINATION lib/${PROJECT_NAME})
else()
  message(STATUS "Hik MVS SDK not found, skipping hik_camera_node")
endif()

# ============================================================================
# 迈德威视相机节点（仅当 MindVision SDK 存在时编译）
# 头文件: /usr/include/CameraApi.h   动态库: /usr/lib/libMVSDK.so
# ============================================================================
if(EXISTS /usr/include/CameraApi.h AND EXISTS /usr/lib/libMVSDK.so)
  message(STATUS "Found MindVision SDK, building mindvision_camera_node")

  add_executable(mindvision_camera_node src/mindvision_camera_node.cpp)
  ament_target_dependencies(mindvision_camera_node rclcpp sensor_msgs std_msgs cv_bridge)
  target_link_libraries(mindvision_camera_node MVSDK ${OpenCV_LIBS})

  install(TARGETS mindvision_camera_node RUNTIME DESTINATION lib/${PROJECT_NAME})
else()
  message(STATUS "MindVision SDK not found, skipping mindvision_camera_node")
endif()

# 安装 launch / config（后续添加内容时目录已就位）
install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
  OPTIONAL
)

if(BUILD_TESTING)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
