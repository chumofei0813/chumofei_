# board_pose_detector 启动文件
# ============================================================================
# 一条命令启动: 相机取流节点 + 目标板位姿解算节点
#
#   迈德威视相机(默认):
#     ros2 launch board_pose_detector board_pose.launch.py
#
#   海康相机:
#     ros2 launch board_pose_detector board_pose.launch.py camera:=hik
#
#   关闭调试图:
#     ros2 launch board_pose_detector board_pose.launch.py publish_debug:=false
#
# 说明: camera 参数决定启动哪个相机节点，并自动加载对应的内参文件
#   (hik -> hik_camera_info.yaml, mindvision -> mindvision_camera_info.yaml)
#   两个相机节点都发布 /image_raw，解算节点无需区分相机来源。
# ============================================================================
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PythonExpression, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare('board_pose_detector')

    # ---- Launch 参数 ----
    camera_arg = DeclareLaunchArgument(
        'camera', default_value='mindvision',
        description="相机类型: 'mindvision'(默认) 或 'hik'"
    )
    publish_debug_arg = DeclareLaunchArgument(
        'publish_debug', default_value='true',
        description="是否发布调试图像 /board_pose/debug_image"
    )

    camera = LaunchConfiguration('camera')

    # 判断条件: 是否为 hik
    is_hik = PythonExpression(["'", camera, "' == 'hik'"])
    is_mindvision = PythonExpression(["'", camera, "' == 'mindvision'"])

    # 参数文件
    board_params = PathJoinSubstitution([pkg_share, 'config', 'board_params.yaml'])

    # 内参文件: 根据 camera 参数拼出 <camera>_camera_info.yaml
    camera_info_path = PathJoinSubstitution([
        pkg_share, 'config',
        PythonExpression(["'", camera, "' + '_camera_info.yaml'"])
    ])

    # ---- 相机节点(二选一) ----
    hik_node = Node(
        package='board_pose_detector',
        executable='hik_camera_node',
        name='hik_camera_node',
        output='screen',
        condition=IfCondition(is_hik),
    )
    mv_node = Node(
        package='board_pose_detector',
        executable='mindvision_camera_node',
        name='mindvision_camera_node',
        output='screen',
        condition=IfCondition(is_mindvision),
    )

    # ---- 位姿解算节点 ----
    pose_node = Node(
        package='board_pose_detector',
        executable='board_pose_node',
        name='board_pose_node',
        output='screen',
        parameters=[
            board_params,
            {
                'camera_info_path': camera_info_path,
                'publish_debug': LaunchConfiguration('publish_debug'),
            }
        ],
    )

    log = LogInfo(msg=['启动相机: ', camera])

    return LaunchDescription([
        camera_arg,
        publish_debug_arg,
        hik_node,
        mv_node,
        pose_node,
        log,
    ])
