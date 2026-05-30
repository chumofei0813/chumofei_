cmake_minimum_required(VERSION 3.8)
project(armor_detector)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)

find_package(rclcpp REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)

find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)          # 用于自定义消息中的 Point

find_package(OpenCV REQUIRED)                 # 添加 OpenCV

find_package(rosidl_default_generators REQUIRED)  # 生成自定义消息

# 生成自定义消息
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/ArmorResult.msg"
  DEPENDENCIES geometry_msgs
)

# 包含头文件目录
include_directories(
  include
  /opt/MVS/include
)

# 海康SDK动态库路径
link_directories(
  /opt/MVS/lib/64
)

# 创建检测器库（方便节点链接）
add_library(${PROJECT_NAME}_core
  src/ArmorDetector.cpp
)
target_include_directories(${PROJECT_NAME}_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)
ament_target_dependencies(${PROJECT_NAME}_core
  rclcpp
)
target_link_libraries(${PROJECT_NAME}_core
  ${OpenCV_LIBS}
)
# 创建节点可执行文件
add_executable(
  armor_detector_node
  src/armor_detector_node.cpp
)

target_link_libraries(
  armor_detector_node 
  ${PROJECT_NAME}_core 
  ${OpenCV_LIBS}
)

ament_target_dependencies(
  armor_detector_node
  rclcpp
  cv_bridge
  sensor_msgs
  std_msgs
  geometry_msgs
  rosidl_default_runtime
  image_transport
)

rosidl_target_interfaces(
  armor_detector_node
  ${PROJECT_NAME} 
  "rosidl_typesupport_cpp"
)

# Hik Camera Node 
add_executable(
  hik_camera_node
  src/hik_camera_node.cpp
)

ament_target_dependencies(
  hik_camera_node
  rclcpp
  sensor_msgs
  cv_bridge
)

target_link_libraries(
  hik_camera_node
  MvCameraControl
  ${OpenCV_LIBS}
)

# 安装
install(
  TARGETS
    ${PROJECT_NAME}_core
    armor_detector_node
    hik_camera_node

  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(DIRECTORY msg
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
