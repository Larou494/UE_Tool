/** 📌 SMaterialParentBatchPanel — 中文操作面板：读取选择 → 筛选勾选 → 预览 → 执行。 */
#pragma once

#include "CoreMinimal.h"
#include "MaterialParentBatchCore.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SMaterialParentBatchPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMaterialParentBatchPanel) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);

private:
    using FEntryPtr = TSharedPtr<MaterialParentBatch::FEntry>;
    TArray<FEntryPtr> Entries;
    TArray<FEntryPtr> VisibleEntries;
    TSharedPtr<SListView<FEntryPtr>> List;
    TStrongObjectPtr<UMaterialInterface> NewParent;
    TStrongObjectPtr<UMaterialInterface> ParentFilter;
    MaterialParentBatch::FPlan Plan;
    MaterialParentBatch::EMode Mode = MaterialParentBatch::EMode::DuplicateAndAssign;
    FString Search;
    FString OutputFolder = TEXT("/Game/MaterialParentBatch/Variants");
    FString Message = TEXT("先在内容浏览器或关卡中选中目标，再读取选择。读取和预览不会修改资产。");
    FString LastBackup;
    bool bPreviewReady = false;

    void InvalidatePreview();
    void Refilter();
    FReply ReadAssets();
    FReply ReadActors();
    FReply Preview();
    FReply Execute();
    FReply CheckVisible(bool bChecked);
    FText Summary() const;
    FText ModeHelp() const;
    TSharedRef<ITableRow> GenerateRow(FEntryPtr Entry, const TSharedRef<STableViewBase>& Owner);
};
