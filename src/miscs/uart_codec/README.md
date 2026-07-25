# tdl_uart_codec

组件包含uart底层驱动管理，8006模组离线语音协议和业务管理，对外提供操作8006语音模组接口。

0.0.1-beta.3修改项
1.修复了关于mic&spk_cfg初始化选择错误的问题。
2.修复了mic_gain_set函数无法实际设置到8006模组底层的问题。
3.将trigger_mode从模块内部剔除，由应用维护。
4.强化了模块init函数对输入cfg参数的检查。
5.增加了mic_status、spk_status变化对外输出回调控制。
6.增加了对于volume和gain设置的参数检查。
7.增加了在8006模组复位(__audio_reset_cb回调)后设置mic_time&silence_timeout操作。

0.0.2-beta.1修改项
1.mcu_ota对外函数适配，但是还未测试。
2.模组的init可重入性优化。

0.0.2-beta.2修改项
1.mcu_ota对外函数适配，并调试，实现了8006模组的ota升级。
2.低功耗适配，增加各模块的deinit函数以及优化init函数的重入性。

0.0.2-beta.3修改项
1.优化了对于唤醒静默超时判断逻辑。