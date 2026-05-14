from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='training_pkg',
            executable='talker',
            name='my_talker',
            arguments=['--ros-args', '--log-level', 'warn'],
            output='screen',
        ),
        Node(
            package='training_pkg',
            executable='listener',
            name='listener',
            output='screen',
        )
    ])