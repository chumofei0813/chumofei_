# armor_detector 启动文件
# ============================================================================
# 支持两种输入源：
#   相机模式（默认）：
#     ros2 launch armor_detector armor_detector.launch.py
#
#   Bag 回放模式：
#     ros2 launch armor_detector armor_detector.launch.py \
#         input_source:=bag \
#         bag_path:=/path/to/your_bag \
#         use_sim_time:=true
#
#   关闭调试：
#     ros2 launch armor_detector armor_detector.launch.py debug:=false
#
#   切换调试等级：
#     ros2 launch armor_detector armor_detector.launch.py debug_level:=3
# ============================================================================

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo, ExecuteProcess
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import LaunchConfiguration, PythonExpression
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    # ---- 包路径与配置文件 ----
    pkg_share = get_package_share_directory('armor_detector')
    config_file = os.path.join(pkg_share, 'config', 'armor_params.yaml')

    # ---- Launch 参数声明 ----
    input_source_arg = DeclareLaunchArgument(
        'input_source', default_value='camera',
        description="输入源: 'camera'（真实相机）或 'bag'（rosbag 回放）"
    )

    bag_path_arg = DeclareLaunchArgument(
        'bag_path', default_value='',
        description="当 input_source='bag' 时，rosbag 文件路径（必填）"
    )

    debug_arg = DeclareLaunchArgument(
        'debug', default_value='true',
        description="是否发布调试图像"
    )

    debug_level_arg = DeclareLaunchArgument(
        'debug_level', default_value='2',
        description="调试等级: 0=off, 1=装甲板框, 2=+灯条, 3=+mask"
    )

    use_sim_time_arg = DeclareLaunchArgument(
        'use_sim_time', default_value='false',
        description="是否使用仿真时钟（bag 模式需要设为 true）"
    )

    # ---- 海康相机节点（仅 camera 模式）----
    hik_camera_node = Node(
        package='armor_detector',
        executable='hik_camera_node',
        name='hik_camera_node',
        output='screen',
        condition=UnlessCondition(
            PythonExpression([
                "'", LaunchConfiguration('input_source'), "' == 'bag'"
            ])
        )
    )

    # ---- Bag 回放节点（仅 bag 模式）----
    bag_play_node = ExecuteProcess(
        cmd=['ros2', 'bag', 'play', LaunchConfiguration('bag_path')],
        output='screen',
        condition=IfCondition(
            PythonExpression([
                "'", LaunchConfiguration('input_source'), "' == 'bag'"
            ])
        )
    )

    # ---- 装甲板检测节点 ----
    armor_detector_node = Node(
        package='armor_detector',
        executable='armor_detector_node',
        name='armor_detector_node',
        output='screen',
        parameters=[
            config_file,
            {
                'use_sim_time': LaunchConfiguration('use_sim_time'),
                'debug': LaunchConfiguration('debug'),
                'debug_level': LaunchConfiguration('debug_level'),
            }
        ]
    )

    # ---- 启动提示 ----
    log_info = LogInfo(
        msg=[
            '输入源: ', LaunchConfiguration('input_source'),
            ' | debug: ', LaunchConfiguration('debug'),
            ' | debug_level: ', LaunchConfiguration('debug_level'),
        ]
    )

    return LaunchDescription([
        input_source_arg,
        bag_path_arg,
        debug_arg,
        debug_level_arg,
        use_sim_time_arg,
        hik_camera_node,
        bag_play_node,
        armor_detector_node,
        log_info,
    ])
