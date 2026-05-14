# 仓库简介
这是一个RM视觉组培训的公开仓库，主要用于记录与验收培训内容。这个仓库会详细记录每一周期的培训内容、实践项目、所遇问题与详细解决方案。  
  
目录：
- [W1](#w1) Ubuntu系统安装、编程环境配置、c++基础
- [W2](#w2) 
- [W3](#w3)
# 周度记录
## W1
### 完成
- [x] 搭建核心操作系统Ubuntu
- [x] 装 VSCode、C/C++ 插件、build-essential、cmake、git
- [x] 在终端里确认 g++、cmake、git 可用
- [x] 写hello world cpp程序并在终端编译运行
- [x] 注册 GitHub 账号，配置本机用户名和邮箱，创建公开仓库
- [x] 学习c++函数、引用、简单类、vector / string / map相关语法
- [x] 用c++写读取命令行参数的问候程序
- [x] 配一个最小 CMakeLists.txt
- [x] 用 build/ 目录完成一次 cmake + make 编译
### C++学习情况及命令行小程序说明
- 学习了c++函数、引用、简单类、vector / string / map相关语法
- 关于程序：greet程序使用标准输出 cout 流式输出问候语到终端，通过 argc 和 argv 实现简单的命令行参数解析。若未提供参数，则输出用法提示 `Usage: ./greet <name>`；若提供参数，则输出 `Hello, <name>!`
- 编译流程：
  1. `mkdir build && cd build`：在当前目录下创建一个名为 build 的子目录,进入这个 build 目录
  2. `cmake ..`:读取上一级目录里的 CMakeLists.txt，然后根据它生成 Makefile 等编译文件
  3. `make`：根据生成的 Makefile 执行编译，生成最终的可执行文件
### 提交
- ['01_function.cpp'](W1/01_function.cpp)
- ['02_map.cpp'](W1/02_map.cpp)
- ['03_class.cpp'](W1/03_class.cpp)
- ['CMakeLists.txt'](W1/CMakeLists.txt)
- ['main.cpp'](W1/greet.cpp)
- ['命令小程序传参截图'](W1/传参截图.png)
- ['version截图'](W1/version.png)
- ['helloworld运行截图'](W1/helloworld.png)
### 问题及解决方法
- `git push` 时提示 `Problem with the SSL CA cert`  

解决方法：执行 `git config --list --show-origin | grep http.ssl`后发现Git全局配置中残留了无效的SSL证书路径,执行 `git config --global --unset http.sslcainfo` 删除该配置，证书错误消失
## W2
### 完成
- [x] ROS2环境配置
- [x] 基础概念学习
- [x] 基础通信
- [x] launch与参数
- [x] bag调试
### 提交
- ['turtlesim运行截图'](W2/turtlesim运行截图.png)
- ['ros2topiclist/info/echo截图'](W2/ros2%20topic%20list%20info%20echo%20截图.png)
- ['talker代码'](W2/talker.cpp)
- ['listener代码'](W2/listener.cpp)
- ['colconbuild截图'](W2/colcon%20build成功截图.png)
- ['talker/listener同时运行截图'](W2/talker%20listener同时运行截图.png)
- ['launch启动，参数生效截图'](W2/launch启动、参数生效截图.png)
- ['ros2baginfo截图'](W2/ros2%20bag%20info截图.png)
- [`launch.py`](W2/talker-listener.launch.py)
### 概念说明
- `workspace`：工作空间，是一个文件夹，里面包含build，install，log，src，用于组织 ROS2 项目。
- `package`：功能包，包含 CMakeLists.txt、package.xml 和源码。
- `node`：节点，一个可执行程序，通过话题、服务等与其他节点通信。
- `topic`：话题，一种异步通信总线，发布者发布消息，订阅者接收消息。
### bag的使用
1. `启动talker`：执行`ros2 run training_pkg talker`
2. `录制`：执行`ros2 bag record /chatter` ,停止后形成rosbag文件夹
3. `查看bag信息`：执行`ros2 bag info`
4. `回放`：执行`ros2 bag play rosbag`,同时执行`ros2 run training_pkg listener`,可以看到listener正确接受并打印了录制时发布的全部消息
### 给talker添加参数的说明
在launch文件中定义了两个节点（talker和listener），并添加参数：  
- `将talker节点改名为my_talker`,ros2 node list会显示新名字
- `设置talker的日志级别为warn`,让talker节点只输出警告及以上级别的日志，从而隐藏 RCLCPP_INFO 打印的发布信息,这样终端不会刷屏，而listener依然能收到消息。
### 问题及解决方法
- vscode中提示`#include错误`，无法打开源文件`rclcpp/rclcpp.hpp`  
可以忽略，不影响实际编译
## W3