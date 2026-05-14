#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = rclcpp::Node::make_shared("talker");
    auto pub = node->create_publisher<std_msgs::msg::String>("chatter", 10);
    rclcpp::WallRate loop_rate(0.5);  // 0.5Hz = 2秒一次

    int count = 0;
    while (rclcpp::ok()) {
        auto msg = std_msgs::msg::String();
        msg.data = "Hello, ROS2! " + std::to_string(count++);
        RCLCPP_INFO(node->get_logger(), "Publishing: '%s'", msg.data.c_str());
        pub->publish(msg);
        rclcpp::spin_some(node);
        loop_rate.sleep();
    }
    rclcpp::shutdown();
    return 0;
}