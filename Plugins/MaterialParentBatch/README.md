# 材质母材质批量替换 · Material Parent Batch

适用于 Unreal Engine 的轻量编辑器插件。从所选模型或关卡物体收集材质实例，去重后批量更换 Parent，省去逐个寻找和打开 MI。插件不进入游戏运行时，也不依赖任何游戏工程的 C++ 模块或美术资产。

1.1.0 以 Windows 64 位、UE 5.5–5.8 为目标，各版本的实际编译和测试记录见 [VALIDATION.md](VALIDATION.md)。

## 安装

**直接使用编译包：** 在 UE_Tool 仓库的 Downloads/v1.1.0 目录下载与你的 UE 次版本对应的压缩包。例如 UE 5.5 使用文件名带 `UE5.5` 的包。关闭目标工程，将压缩包里的 `MaterialParentBatch` 文件夹放到工程的 `Plugins` 目录；最终应存在 `你的工程/Plugins/MaterialParentBatch/MaterialParentBatch.uplugin`。重新打开工程即可。

**使用源码：** 下载独立源码包后，解压并把其中的 `MaterialParentBatch` 文件夹放到工程的 `Plugins` 目录。如果下载的是整个 `UE_Tool` 仓库，则复制仓库内的 `Plugins/MaterialParentBatch` 文件夹。最终路径应为 `你的工程/Plugins/MaterialParentBatch/MaterialParentBatch.uplugin`。使用对应版本 UE 与 C++ 工具链编译。纯蓝图工程可以先用下面的独立构建脚本生成编译文件，再安装插件。

每个 UE 次版本使用单独编译的 DLL。不要把 5.5 的 `Binaries` 复制给 5.6、5.7 或 5.8；不同引擎构建号也可能要求重新编译。源码描述文件不固定单一 `EngineVersion`，发布的编译包会标记实际构建版本。

## 打开

在顶部 **工具 → 材质母材质批量替换** 打开面板。若没有入口，在 **编辑 → 插件** 搜索 `Material Parent Batch`，启用后保存工作并重启编辑器。

## 使用

1. 在内容浏览器多选静态网格体、骨骼网格体或 MI，然后读取内容浏览器选择；或者在关卡选择 Actor，读取关卡选中物体。再次读取会替换上一份清单。
2. 选择已经准备好的新母材质，可以选择材质或材质实例。
3. 选择操作模式，默认复制实例，也可选择修改原实例。
4. 按原父级（仅直接 Parent）或名称、路径筛选，勾选目标。只有当前显示且勾选的条目参与执行。
5. 生成预览，检查输出路径、槽数和问题。悬停槽数可查看对应目标与槽号。
6. 对不兼容的条目取消勾选，或者调整母材质后重新预览。预检通过后才能执行。
7. 执行已预览操作，检查结果后自行保存。插件不会自动保存资产或提交 Git。

## 两种模式的影响范围

| 选择来源 | 修改原实例 | 复制实例并替换所选目标 |
| --- | --- | --- |
| 内容浏览器中的静态、骨骼网格体 | 所有引用该 MI 的地方一起变化 | 替换所选网格体资产的材质槽；使用这些网格体的关卡物体也可能随之变化 |
| 关卡中的 Actor | 所有引用该 MI 的地方一起变化 | 只替换所选 Actor 的组件材质覆盖，不修改网格体资产或其他 Actor |
| 直接选择 MI | 修改原 MI | 只生成副本，不自动替换外部引用 |

同一个原实例在一批操作中只复制一次，所选目标共用该副本。默认输出目录为 `/Game/MaterialParentBatch/Variants`，名称追加 `_Variant`，遇到重名会生成唯一名称，不覆盖已有资产。

普通材质、动态材质实例和空槽会被忽略。ISM/HISM 按整个组件处理，不针对其中的单个实例。

## 参数保护

保留显式设置且在新母材质中具有相同名称与类型的参数覆盖，包括标量、颜色、纹理和静态开关。继承值使用新母材质的默认值。母材质逻辑变化仍可能造成外观变化。

缺失参数或类型不兼容会阻止执行并显示问题；工具不会猜测参数重命名关系。当前版本不支持 Material Layers 堆栈和 Curve Atlas 参数。引擎或第三方插件目录中的源 MI、需要改写的模型应先复制到 `/Game/`。

生成预览后，如果原父级、参数或目标材质槽发生变化，执行会拒绝过期预览，需要重新生成。

## 撤销与备份

执行前在 `Saved/MaterialParentBatch/Backups/时间-标识/` 备份磁盘上的原始包文件，以及记录原父级、新父级、目标和材质槽的清单。备份失败会中止执行。

整批操作使用一个编辑器撤销事务，可用 Ctrl+Z 撤销父级修改和材质槽替换。新创建的副本可能保留，确认不再引用后再手动删除。执行中途出错时会停止后续条目，并报告已完成的数量。

磁盘备份只包含最后保存的版本；操作前已有的未保存修改依赖编辑器撤销。请先保存重要工作。`Saved` 不提交到 Git，清理该目录前请另存需要保留的备份。

需要从磁盘备份恢复时，先关闭加载这些资产的编辑器，再按清单还原到原路径；已提交的版本也可通过 Git 恢复。

## 实现与验证

使用 `UMaterialEditorInstanceConstant::PostEditChangeProperty` 的编辑器修改路径，并核对参数覆盖，避开 UE 5.5 中直接调用 `SetMaterialInstanceParent` 的已知问题 UE-269068。

自动化测试位于 Session Frontend → Automation → MaterialParentBatch，使用测试资产，不保存生产资产。构建与测试记录见 [VALIDATION.md](VALIDATION.md)。

源码位于 `Source/MaterialParentBatch`。`Binaries/Win64` 中的编译产物被 Git 忽略，通过 Downloads 目录中版本对应的安装包分发。

## 独立构建与测试

在安装了 UE 和对应 C++ 工具链的 Windows 电脑上，以 PowerShell 运行：

```powershell
./Scripts/BuildAndTest.ps1 -EngineRoot 'C:/Program Files/Epic Games/UE_5.5'
```

把路径换成 5.6、5.7 或 5.8 的引擎目录即可测试对应版本。脚本会在 `Artifacts/UE_版本/HostProject` 创建隔离工程，按该引擎最新默认设置编译，再以 DX12 离屏编辑器运行 6 项自动化测试。`Build.log`、`Automation.log`、`TestReport` 和 `Result.json` 保存在对应版本目录。

只编译时可加 `-SkipTests`；这种结果不会被发布打包脚本接受为已测试版本。脚本不打开或升级你的游戏工程。

需要打包时，完成对应版本测试后运行 `./Scripts/Package.ps1`。脚本只接受已通过六项测试、源码与当前仓库一致的构建，生成独立源码包及各版本 Win64 安装包，并写入 SHA-256 校验清单。

## 仅在本地使用

如不希望插件进入游戏项目的 Git 仓库，在该项目的 `.git/info/exclude` 末尾添加：

```gitignore
/Plugins/MaterialParentBatch/
```

该规则只在本地生效，不会上传。若插件已暂存但尚未提交，可用 `git restore --staged -- Plugins/MaterialParentBatch` 取消暂存，文件仍留在磁盘。已经提交的插件不能仅靠忽略规则停止跟踪。插件生成、修改的游戏资产仍按工程原有方式管理。
