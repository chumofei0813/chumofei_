#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = rclcpp::Node::make_shared("talker");

    rcl_interfaces::msg::ParameterDescriptor desc;
    desc.description = "Publish frequency in Hz";
    desc.integer_range.resize(1);
    desc.integer_range[0].from_value = 1;
    desc.integer_range[0].to_value = 10;
    node->declare_parameter<int>("frequency_hz", 2, desc);
    int hz = node->get_parameter("frequency_hz").as_int();

    auto pub = node->create_publisher<std_msgs::msg::String>("chatter", 10);
    rclcpp::WallRate loop_rate(hz);

    int count = 0;
    while (rclcpp::ok()) {
        auto msg = std_msgs::msg::String();
        msg.data = "Hello, ROS2! " + std::to_string(count++);
        RCLCPP_INFO(node->get_logger(), "Publishing: '%s' (freq=%d Hz)", msg.data.c_str(), hz);
        pub->publish(msg);
        rclcpp::spin_some(node);
        loop_rate.sleep();
    }
    rclcpp::shutdown();
    return 0;
}