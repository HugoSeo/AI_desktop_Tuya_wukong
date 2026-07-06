# Quick Start

This guide introduces the **Wukong AI Hardware Development Framework** and this sample (**tuyaos_demo_wukong_ai**): concepts, environment setup, product creation, obtaining the framework, configuration, build, and flash.

## 1. Prerequisites: Board and Environment

### Hardware

- **T5 development board**: Obtain via Tuya or designated channels; follow current platform instructions.
- **Type-C cable**: To connect the board to your PC.
- **Speaker**: JST GH 1.25 mm 2-pin connector for audio playback.
- **PC**.

### Software

- **Tuya Wind IDE**: Install on the PC for obtaining the framework, building, and flashing. Use **Windows host + Linux VM** or **Linux only**.
- **USB‑to‑serial driver**: Install on the PC so the board’s serial port is recognized.
- **TuyaOS**: Familiarity with the general TuyaOS development flow is recommended.

## 2. Step 1: Create a Product

To create an AI hardware product, you need to complete the normal Tuya **product creation** flow and additionally **create and bind an agent**:

1. Log in to the Tuya developer platform and create a product (choose category and connectivity). Obtain the **product PID**.
2. **Create and bind an agent** for that product (AI and dialogue settings).
3. In the product development/debug area, obtain the product **authorization info** (e.g. UUID, authKey). You will use this PID and authorization in the framework project so the device can provision and work correctly.

After this, proceed to “Get the development framework” and “Modify PID and authorization”.

## 3. Step 2: Get the Development Framework

Use Tuya Wind IDE to obtain the Wukong AI hardware development framework.

### 3.1 Apply for permission

1. Start Tuya Wind IDE and open **Resource Center**.
2. Select **Wukong AI hardware development framework**, then click **Apply for permission**.
3. Enter your Tuya business email and submit. After approval, continue to the next step.

![Apply for permission](https://images.tuyacn.com/fe-static/docs/img/21e6faec-c2b7-4fc1-a693-be86782b4dd8.png)

### 3.2 Create development framework

1. After approval, on the IDE home page click **Create development framework**.
2. Follow the wizard to choose platform and options (e.g. chip platform, sample app), then click **Finish** to create the framework.
3. The IDE will download the framework; the tree will include a Demo app (e.g. **tuyaos_demo_wukong_ai** or similar).

![Create development framework](https://images.tuyacn.com/fe-static/docs/img/a5b1b509-1652-4dc8-b867-e962aa77f776.png)

### 3.3 Modify PID and authorization

The default PID and authorization in the Demo are not usable (already in use). Replace them with the product PID and authorization from Step 1:

1. Open the downloaded framework project in Tuya Wind IDE.
2. Locate the project files where PID and authorization are configured (usually product or key config; see your SDK docs).
3. Set **PID** to your product’s PID from the Tuya platform.
4. Set **authorization** (e.g. UUID, authKey) to that product’s authorization from the platform.
5. Save and rebuild the project so the new config is used.

## 4. Step 3: Configure and Build

1. **Configure the app** (board, features)  
   In the IDE, under `software > TuyaOS > apps`, find the Demo (e.g. **tuyaos_demo_wukong_ai**). You can run menuconfig from the command line at the SDK root:
   ```bash
   make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
   ```
   The sample supports voice + UI; enable camera in config if needed, or disable UI for voice-only.

2. **Generate app config headers** (required after config changes):
   ```bash
   make app_config APP_NAME=tuyaos_demo_wukong_ai
   ```

3. **Build**  
   - In the IDE: in the explorer, right‑click the Demo folder and choose **Build Project**. Enter a version and press Enter to start the build.  
   - Or at the SDK root:
   ```bash
   make app APP_NAME=tuyaos_demo_wukong_ai
   ```
   - **First build**: Downloads environment, toolchain, and build setup; it can take a long time.
   - **Output**: After a successful build, the QIO image is under `software/TuyaOS/apps/tuyaos_demo_wukong_ai/output/<version>/`, e.g. `tuyaos_demo_wukong_ai_QIO_<version>.bin`.

![Build success](https://images.tuyacn.com/fe-static/docs/img/fa92004a-2b98-429a-9bd3-6b595ec6b361.png)

4. **Build failures**  
   If the failure is due to missing environment or dependencies:
   - Install build dependencies on Linux (example):
     ```bash
     sudo dpkg --add-architecture i386
     sudo apt-get update
     sudo apt-get install build-essential cmake python3 python3-pip doxygen ninja-build libc6:i386 libstdc++6:i386 libncurses5-dev lib32z1 -y
     sudo pip3 install sphinx_rtd_theme future breathe blockdiag sphinxcontrib-seqdiag sphinxcontrib-actdiag sphinxcontrib-nwdiag sphinxcontrib.blockdiag
     ```
   - Common Python errors:

     | Error | Fix |
     |-------|-----|
     | `ModuleNotFoundError: No module named 'click'` | `pip install click` |
     | `ModuleNotFoundError: No module named 'Crypto'` | `pip install pycryptodome` |
     | `ModuleNotFoundError: No module named 'ruamel'` | `pip3 install ruamel.yaml` |

   If issues persist, post on the TuyaOS Developer Forum Connected Device section.

![Build troubleshooting](https://images.tuyacn.com/fe-static/docs/img/3fa2df15-f1d6-4df4-ac9c-f2fd37ad9a2f.png)

## 5. Step 4: Flash the Firmware

### 5.1 Connect the device

- Connect the board to the PC with the **Type-C cable** and expose the USB serial port to Linux (e.g. in the VM).
- On VMware + Linux: **VM** → **Removable devices** → select **QinHeng USB Dual_Serial** (or your serial device) → **Connect**.
- If the serial port is not accessible, run `sudo usermod -aG dialout $USER`, then **reboot the VM** and try again.

![Connect device](https://images.tuyacn.com/fe-static/docs/img/58301e6a-1e4a-4746-9ace-c38074bc346d.png)

### 5.2 Flash the firmware

1. In Tuya Wind IDE, locate the built **QIO image** (e.g. `software > TuyaOS > apps > tuyaos_demo_wukong_ai > output/<version> > tuyaos_demo_wukong_ai_QIO_<version>.bin`). Right‑click it and choose **Flash App**.
2. Select the serial port (usually `ttyACM0`).
3. If the terminal stays at `Waiting Reset ...`, the board does not support auto‑reset; **press the board’s RST button** to continue.
4. After the device resets, the IDE will start flashing. When the terminal shows completion, flashing is done.

![Flash App](https://images.tuyacn.com/fe-static/docs/img/ad517df6-efd7-4e01-a456-b129061eef01.png)  
![Flash done](https://images.tuyacn.com/fe-static/docs/img/9fa80aff-cabf-451b-a107-0e7ba50f2c70.png)

## 6. Demo

After flashing, power cycle the board and use the **Tuya Smart app** to provision the device:

1. Open the Tuya Smart app and start adding a device.
2. Choose the device type (e.g. Wi‑Fi device) and follow the app to put the device in provisioning mode (e.g. hold the provisioning button).
3. Enter your Wi‑Fi password and wait for the device to connect and complete provisioning.
4. Once provisioned, you can use the app to control the device and try voice, keys, display, etc.

## 7. Next Steps

- **Framework description and doc index**: [README.md](../README.md)
- **Domain and module docs**: See the “Source and Document Index” table there, then open [Wukong core](../src/wukong/README_CN.md), [Mode](../src/mode/README_CN.md), [Boards](../src/boards/README_CN.md), etc. as needed.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
