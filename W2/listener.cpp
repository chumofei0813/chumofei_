#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = rclcpp::Node::make_shared("listener");
    auto sub = node->create_subscription<std_msgs::msg::String>(
        "chatter", 10, [](const std_msgs::msg::String::SharedPtr msg) {
            RCLCPP_INFO(rclcpp::get_logger("listener"), "I heard: '%s'", msg->data.c_str());
        });
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}