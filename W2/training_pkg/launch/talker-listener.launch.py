from launch import LaunchDescription
from launch_ros.actions import Node
import os
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    params_file = os.path.join(
        get_package_share_directory('training_pkg'),
        'config',
        'talker_params.yaml'
    )
    return LaunchDescription([
        Node(
            package='training_pkg',
            executable='talker',
            name='my_talker',
            parameters=[params_file],
            output='screen',
        ),
        Node(
            package='training_pkg',
            executable='listener',
            name='listener',
            output='screen',
        )
    ])