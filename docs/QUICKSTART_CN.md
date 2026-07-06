# 快速开始

本文介绍如何快速了解 **Wukong AI 硬件开发框架** 与本示例（tuyaos_demo_wukong_ai）的相关概念与开发流程，从环境准备、创建产品、获取框架、配置编译到烧录运行。

## 开发板环境搭建

### 硬件环境

- **T5 开发板**：如需获取 T5 开发板，请通过涂鸦或指定渠道申请，具体以当前平台说明为准。
- **Type-C 接口数据线**：用于连接开发板与 PC。
- **扬声器**：接口为 JST GH 1.25mm 2P，用于音频播放。
- **个人电脑（PC）**。

### 软件环境

- **Tuya Wind IDE**：在 PC 上安装，用于获取框架、编译与烧录。安装环境为 **Windows 主机 + Linux 虚拟机** 或 **纯 Linux**。
- **USB 转串口驱动**：在 PC 上安装，以便识别开发板串口。
- **TuyaOS 开发**：建议先了解 TuyaOS 整体开发流程，便于理解框架与编译步骤。

## 第一步：创建产品

创建 AI 硬件产品时，需要在原有的涂鸦 [创建产品](https://developer.tuya.com/cn/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc) 流程的基础上，额外新增**智能体的创建和绑定**工作。目前，涂鸦开发者平台已经全面上架 AI 硬件产品开发能力。具体操作流程请参考 [产品 AI 功能开发](https://developer.tuya.com/cn/docs/iot/AI-feature?id=Keapy1et1fc63)。

简要步骤如下：

1. 登录涂鸦开发者平台，进入 [创建产品](https://developer.tuya.com/cn/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc) 流程，选择对应产品类目与联网方式，创建产品并获取 **产品 PID**。
2. 在平台中为该产品**创建并绑定智能体**（AI 能力与对话配置等），详见 [产品 AI 功能开发](https://developer.tuya.com/cn/docs/iot/AI-feature?id=Keapy1et1fc63)。
3. 在产品开发->硬件开发页面，免费获取该产品的 **授权信息**（如 UUID、authKey 等），后续在框架工程中填入此 PID 与授权信息，设备才能正常配网与使用。

完成以上步骤后，再进入「获取开发框架」与「修改 PID 和授权信息」步骤。

## 第二步：获取开发框架

通过 Tuya Wind IDE 获取 Wukong AI 硬件开发框架，流程如下。

### 3.1 申请权限

1. 启动 Tuya Wind IDE，进入 **资源中心**。
2. 按界面选项选择 **Wukong AI 硬件开发框架**，完成后单击 **申请权限**。
3. 填写涂鸦商务邮箱并提交，发起开发框架权限申请。审批通过后可进行下一步。

![申请权限](https://images.tuyacn.com/fe-static/docs/img/21e6faec-c2b7-4fc1-a693-be86782b4dd8.png)

### 3.2 创建开发框架

1. 审批通过后，在 IDE 主页单击 **创建开发框架**。
2. 按向导选择平台与选项（如芯片平台、示例应用等），单击 **完成** 创建开发框架。
3. 创建完成后，Tuya Wind IDE 将自动下载框架，目录中会包含 Demo 应用（如 tuyaos_demo_wukong_ai 或类似名称）。

![创建开发框架](https://images.tuyacn.com/fe-static/docs/img/a5b1b509-1652-4dc8-b867-e962aa77f776.png)

### 3.3 修改 PID 和授权信息

Demo 中默认的 PID 和授权信息尽量避免直接使用（仅为示例程序使用），必须改为你在「第一步」中创建的产品 PID 与授权信息。操作步骤：

1. 在 Tuya Wind IDE 中打开已下载的框架工程。
2. 找到工程中配置 PID 与授权信息的文件（通常为产品配置或密钥相关头文件/配置文件，具体以当前 SDK 说明为准）。
3. 将 **PID** 替换为你在涂鸦平台创建的产品 PID。
4. 将 **授权信息**（如 UUID、authKey 等）替换为该产品在平台上的授权信息。
5. 保存后重新编译工程，使配置生效。

## 第三步：配置与编译

1. **配置应用**（选择板型、功能开关等）  
   在 Tuya Wind IDE 中，根据路径 `software > TuyaOS > apps` 找到 Demo（如 **tuyaos_demo_wukong_ai**）。可先通过菜单或脚本执行配置（若使用命令行，在 SDK 根目录执行）：
   ```bash
   make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
   ```
   示例支持语音 + UI，摄像头需在配置中打开；也可关闭 UI 仅保留语音等，按需选择。

2. **生成应用配置头文件**（配置修改后必须执行）  
   ```bash
   make app_config APP_NAME=tuyaos_demo_wukong_ai
   ```

3. **编译固件**  
   - 在 Tuya Wind IDE 中：在资源管理器中找到上述 Demo 目录，**右键** 选择 **Build Project**，输入编译版本号后回车，开始编译。  
   - 或在 SDK 根目录执行：
   ```bash
   make app APP_NAME=tuyaos_demo_wukong_ai
   ```
   - **首次编译**：会拉取开发环境、解压工具链、构建虚拟编译环境，耗时较长，请耐心等待。
   - **固件产出**：编译成功后，在 `software/TuyaOS/apps/tuyaos_demo_wukong_ai/output/<版本>/` 下会生成目标 QIO 固件，例如 `tuyaos_demo_wukong_ai_QIO_<版本>.bin`。

![编译成功](https://images.tuyacn.com/fe-static/docs/img/fa92004a-2b98-429a-9bd3-6b595ec6b361.png)

4. **编译失败时**  
   若为虚拟环境或依赖缺失，可参考以下排查：
   - 在 Linux 终端安装 Python、CMake、Ninja 及依赖（示例）：
     ```bash
     sudo dpkg --add-architecture i386
     sudo apt-get update
     sudo apt-get install build-essential cmake python3 python3-pip doxygen ninja-build libc6:i386 libstdc++6:i386 libncurses5-dev lib32z1 -y
     sudo pip3 install sphinx_rtd_theme future breathe blockdiag sphinxcontrib-seqdiag sphinxcontrib-actdiag sphinxcontrib-nwdiag sphinxcontrib.blockdiag
     ```
   - 常见 Python 报错与处理：

     | 报错 | 解决方法 |
     |------|----------|
     | `ModuleNotFoundError: No module named 'click'` | `pip install click` |
     | `ModuleNotFoundError: No module named 'Crypto'` | `pip install pycryptodome` |
     | `ModuleNotFoundError: No module named 'ruamel'` | `pip3 install ruamel.yaml` |

   若以上方法仍无法解决，可到 TuyaOS 开发者论坛联网单品开发版块发帖咨询。

![编译失败排查](https://images.tuyacn.com/fe-static/docs/img/3fa2df15-f1d6-4df4-ac9c-f2fd37ad9a2f.png)

## 第四步：烧录固件

### 连接设备

- 将开发板通过 **Type-C 数据线** 与 PC 连接，并将 USB 串口映射到 Linux（若使用虚拟机）。
- 以 VMware + Linux 为例：在 VMware 菜单选择 **虚拟机** → **可移动设备** → 选择对应的 **QinHeng USB Dual_Serial**（或本机显示的串口设备）→ **连接**。
- 若从未配置过虚拟机串口权限，需先执行 `sudo usermod -aG dialout $USER`，然后**重启虚拟机**，再重新连接设备。

![连接设备](https://images.tuyacn.com/fe-static/docs/img/58301e6a-1e4a-4746-9ace-c38074bc346d.png)

### 烧录固件

1. 在 Tuya Wind IDE 中找到生成的 **QIO 固件**，路径示例：`software > TuyaOS > apps > tuyaos_demo_wukong_ai > output/<版本> > tuyaos_demo_wukong_ai_QIO_<版本>.bin`。右键该文件，选择 **Flash App**。
2. 选择串口号，一般选择 `ttyACM0`。
3. 若终端停留在 `Waiting Reset ...`，说明当前硬件不支持自复位，需**手动按下开发板上的复位（RST）按键**，再继续烧录。
4. 设备重启后，Tuya Wind IDE 会开始烧录。终端出现烧录完成提示时，表示烧录已成功。

![烧录固件](https://images.tuyacn.com/fe-static/docs/img/ad517df6-efd7-4e01-a456-b129061eef01.png)  
![烧录完成](https://images.tuyacn.com/fe-static/docs/img/9fa80aff-cabf-451b-a107-0e7ba50f2c70.png)

## 功能演示

烧录完成后，重启开发板，使用 **涂鸦智能 App** 对设备进行配网：

1. 打开涂鸦智能 App，进入添加设备流程。
2. 根据设备类型选择「Wi-Fi 设备」或对应品类，按 App 提示使设备进入配网模式（如长按配网键等）。
3. 输入当前 Wi-Fi 密码，等待设备连接云端并完成配网。
4. 配网成功后即可在 App 中查看设备并进行语音、按键、显示等功能验证。


## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
