from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():

    config_file = os.path.join(
        get_package_share_directory('armor_detector'),
        'config',
        'armor_params.yaml'
    )

    hik_camera_node = Node(
        package='armor_detector',
        executable='hik_camera_node',
        name='hik_camera_node',
        output='screen'
    )

    armor_detector_node = Node(
        package='armor_detector',
        executable='armor_detector_node',
        name='armor_detector_node',
        output='screen',
        parameters=[config_file]
    )

    return LaunchDescription([
        hik_camera_node,
        armor_detector_node
    ])