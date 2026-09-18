/** 📌 MaterialParentBatchModule — 仅在编辑器加载，注册“工具 → 材质母材质批量替换”。 */
#include "Modules/ModuleManager.h"
#include "SMaterialParentBatchPanel.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "MaterialParentBatch"

class FMaterialParentBatchModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // 📌 命令行测试无需注册 Slate 菜单，但核心模块和测试仍正常加载。
        if (IsRunningCommandlet()) { return; }
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("MaterialParentBatch"),
            FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& Args)
            { return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SMaterialParentBatchPanel)]; }))
            .SetDisplayName(LOCTEXT("TabTitle", "材质母材质批量替换"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMaterialParentBatchModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        if (IsRunningCommandlet()) { return; }
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("MaterialParentBatch"));
    }

private:
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("MaterialParentBatch"));
        Section.AddMenuEntry(TEXT("OpenMaterialParentBatch"), LOCTEXT("Open", "材质母材质批量替换"),
            LOCTEXT("Tooltip", "从所选模型、材质实例或关卡物体收集 MI，预览后批量更换父级。"), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("MaterialParentBatch"))); })));
    }
};

IMPLEMENT_MODULE(FMaterialParentBatchModule, MaterialParentBatch)
#undef LOCTEXT_NAMESPACE
