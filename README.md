# UE_Tool

LaRou 的 Unreal Engine 编辑器工具。目前包含 **材质母材质批量替换（Material Parent Batch）1.1.0**。

从选中的静态模型、骨骼模型、材质实例或关卡物体收集材质实例，预览后批量替换母材质。支持修改原实例，以及复制实例后只替换所选目标。

## 下载与安装

按你的 UE 次版本选择对应安装包。打开文件链接后，点击 GitHub 文件页右上方的 **Download raw file（下载原始文件）** 图标。

| 引擎版本 | Win64 安装包 | 已实测版本 |
| --- | --- | --- |
| UE 5.5 | [下载 5.5 安装包](Downloads/v1.1.0/MaterialParentBatch_v1.1.0_UE5.5_Win64.zip) | 5.5.4 |
| UE 5.6 | [下载 5.6 安装包](Downloads/v1.1.0/MaterialParentBatch_v1.1.0_UE5.6_Win64.zip) | 5.6.1 |
| UE 5.7 | [下载 5.7 安装包](Downloads/v1.1.0/MaterialParentBatch_v1.1.0_UE5.7_Win64.zip) | 5.7.4 |
| UE 5.8 | [下载 5.8 安装包](Downloads/v1.1.0/MaterialParentBatch_v1.1.0_UE5.8_Win64.zip) | 5.8.2 |

也可下载 [独立源码包](Downloads/v1.1.0/MaterialParentBatch_v1.1.0_Source.zip)；[SHA-256 校验清单](Downloads/v1.1.0/SHA256SUMS.json) 用于核对下载内容。

1. 关闭目标 UE 工程。
2. 解压所选安装包，把 `MaterialParentBatch` 文件夹放到工程的 `Plugins` 文件夹中。
3. 确认路径为 `你的工程/Plugins/MaterialParentBatch/MaterialParentBatch.uplugin`。
4. 打开工程，从 **工具 → 材质母材质批量替换** 进入。若没有菜单，在 **编辑 → 插件** 搜索 `Material Parent Batch`，启用后重启。

每个引擎版本使用对应 DLL。不同引擎构建号可能需要从源码重新编译。

## 快速使用

在关卡选中需要处理的物体，点击 **读取关卡选中物体**，选择 **新母材质**，保留默认的 **复制实例，仅应用于所选目标**。勾选条目后点击 **生成预览 → 执行已预览操作**，确认效果后自行保存。

从内容浏览器读取模型会修改模型资产的材质槽；切换到修改原实例模式会影响该实例的所有引用者。兼容的同名同类型参数覆盖会保留，不兼容的参数会阻止执行。

## 文档与源码

- [完整使用说明与本地 Git 忽略方法](Plugins/MaterialParentBatch/README.md)
- [四版本编译与测试记录](Plugins/MaterialParentBatch/VALIDATION.md)：每个版本 6 项测试通过，共 24 项。
- [版本更新记录](Plugins/MaterialParentBatch/CHANGELOG.md)
- [插件源码](Plugins/MaterialParentBatch/Source/MaterialParentBatch)
- [独立构建测试脚本](Plugins/MaterialParentBatch/Scripts/BuildAndTest.ps1)

测试使用独立工程和测试资产，没有修改原游戏工程。插件不会自动保存资产；执行前备份已保存的包文件，修改支持编辑器撤销。Material Layers 和 Curve Atlas 参数暂不支持，详见完整说明。