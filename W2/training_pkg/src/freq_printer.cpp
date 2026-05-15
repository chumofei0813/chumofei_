#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include <chrono>

class FreqPrinter : public rclcpp::Node {
public:
    FreqPrinter() : Node("freq_printer"), count_(0), last_time_(this->now()) {
        sub_ = this->create_subscription<std_msgs::msg::String>(
            "chatter", 10,
            std::bind(&FreqPrinter::callback, this, std::placeholders::_1));
        timer_ = this->create_wall_timer(
            std::chrono::seconds(2),
            std::bind(&FreqPrinter::print_avg, this));
    }

private:
    void callback(const std_msgs::msg::String::SharedPtr /*msg*/) {
        count_++;
        auto now = this->now();
        double dt = (now - last_time_).seconds();
        if (dt > 0) {
            double inst_freq = 1.0 / dt;
            RCLCPP_INFO(this->get_logger(), "Instant freq: %.2f Hz", inst_freq);
        }
        last_time_ = now;
    }

    void print_avg() {
        double avg_freq = count_ / 2.0;
        RCLCPP_INFO(this->get_logger(), "Average freq (last 2s): %.2f Hz", avg_freq);
        count_ = 0;
    }

    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_;
    rclcpp::TimerBase::SharedPtr timer_;
    int count_;
    rclcpp::Time last_time_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<FreqPrinter>());
    rclcpp::shutdown();
    return 0;
}