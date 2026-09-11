# 禁止使用

该2026-09-03构建存在HJ212发送阻断问题：完整帧QN无法被发送前校验器识别，产生
`request_identity_missing`，不会执行`TX_ATTEMPT`。

请改用：

`..\2026-09-04_hj212-send-hotfix_compiled-not-hardware-verified`
