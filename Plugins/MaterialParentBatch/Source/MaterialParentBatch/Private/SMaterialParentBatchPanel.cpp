/**
 * 📌 SMaterialParentBatchPanel.cpp — 只对当前可见且勾选的条目生成操作计划。
 * 修改过滤条件、模式、目标母材质或输出目录后，必须重新预览。
 */
#include "SMaterialParentBatchPanel.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MaterialParentBatch"

void SMaterialParentBatchPanel::Construct(const FArguments& Args)
{
    ChildSlot.Padding(12)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [ SNew(STextBlock).Text(LOCTEXT("Title", "批量更换母材质" )).Font(FCoreStyle::GetDefaultFontStyle("Bold", 18)) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [ SNew(SButton).Text(LOCTEXT("ReadAssets", "读取内容浏览器选择" )).OnClicked(this, &SMaterialParentBatchPanel::ReadAssets) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [ SNew(SButton).Text(LOCTEXT("ReadActors", "读取关卡选中物体" )).OnClicked(this, &SMaterialParentBatchPanel::ReadActors) ]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(this, &SMaterialParentBatchPanel::Summary) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(STextBlock).Text(LOCTEXT("NewParent", "新母材质")) ]
            + SHorizontalBox::Slot().FillWidth(1)
            [
                SNew(SObjectPropertyEntryBox).AllowedClass(UMaterialInterface::StaticClass())
                .ObjectPath_Lambda([this] { return NewParent.IsValid() ? NewParent->GetPathName() : FString(); })
                .OnObjectChanged_Lambda([this](const FAssetData& Asset)
                { NewParent.Reset(Cast<UMaterialInterface>(Asset.GetAsset())); InvalidatePreview(); })
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)
            [
                SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
                .IsChecked_Lambda([this] { return Mode == MaterialParentBatch::EMode::DuplicateAndAssign ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                { if (State == ECheckBoxState::Checked) { Mode = MaterialParentBatch::EMode::DuplicateAndAssign; InvalidatePreview(); } })
                [ SNew(STextBlock).Text(LOCTEXT("CopyMode", "复制实例，仅应用于所选目标")) ]
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
                .IsChecked_Lambda([this] { return Mode == MaterialParentBatch::EMode::ModifyOriginal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                { if (State == ECheckBoxState::Checked) { Mode = MaterialParentBatch::EMode::ModifyOriginal; InvalidatePreview(); } })
                [ SNew(STextBlock).Text(LOCTEXT("OriginalMode", "修改原实例，影响所有引用者")) ]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [ SNew(STextBlock).AutoWrapText(true).Text(this, &SMaterialParentBatchPanel::ModeHelp) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(STextBlock).Text(LOCTEXT("Output", "副本目录")) ]
            + SHorizontalBox::Slot().FillWidth(1)
            [ SNew(SEditableTextBox).Text(FText::FromString(OutputFolder))
                .IsEnabled_Lambda([this] { return Mode == MaterialParentBatch::EMode::DuplicateAndAssign; })
                .OnTextChanged_Lambda([this](const FText& Text) { OutputFolder = Text.ToString().TrimStartAndEnd(); OutputFolder.RemoveFromEnd(TEXT("/")); InvalidatePreview(); }) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [ SNew(STextBlock).Text(LOCTEXT("OldParent", "筛选原父级（可选）")) ]
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 10, 0)
            [ SNew(SObjectPropertyEntryBox).AllowedClass(UMaterialInterface::StaticClass()).AllowClear(true)
                .ObjectPath_Lambda([this] { return ParentFilter.IsValid() ? ParentFilter->GetPathName() : FString(); })
                .OnObjectChanged_Lambda([this](const FAssetData& Asset) { ParentFilter.Reset(Cast<UMaterialInterface>(Asset.GetAsset())); Refilter(); }) ]
            + SHorizontalBox::Slot().FillWidth(1)
            [ SNew(SSearchBox).HintText(LOCTEXT("Search", "按实例名或路径筛选"))
                .OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); Refilter(); }) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [ SNew(SButton).Text(LOCTEXT("All", "全选当前列表")).OnClicked(this, &SMaterialParentBatchPanel::CheckVisible, true) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("None", "取消当前列表勾选")).OnClicked(this, &SMaterialParentBatchPanel::CheckVisible, false) ]
        ]
        + SVerticalBox::Slot().FillHeight(1).MinHeight(140)
        [ SAssignNew(List, SListView<FEntryPtr>).ListItemsSource(&VisibleEntries)
            .SelectionMode(ESelectionMode::None).OnGenerateRow(this, &SMaterialParentBatchPanel::GenerateRow) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 8)
        [ SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("Parameters", "保留实例自身覆盖的同名同类型参数；未覆盖参数继承新父级。参数不兼容时阻止执行，并在列表中说明。")) ]
        + SVerticalBox::Slot().AutoHeight().MaxHeight(150)
        [ SNew(SScrollBox) + SScrollBox::Slot()
            [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Message); }) ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
            [ SNew(SButton).Text(LOCTEXT("Preview", "1. 生成预览")).OnClicked(this, &SMaterialParentBatchPanel::Preview) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
            [ SNew(SButton).Text(LOCTEXT("Execute", "2. 执行已预览操作"))
                .IsEnabled_Lambda([this] { return bPreviewReady && GEditor && !GEditor->PlayWorld; })
                .OnClicked(this, &SMaterialParentBatchPanel::Execute) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Text(LOCTEXT("Backup", "打开本次备份"))
                .IsEnabled_Lambda([this] { return !LastBackup.IsEmpty(); })
                .OnClicked_Lambda([this] { FPlatformProcess::ExploreFolder(*LastBackup); return FReply::Handled(); }) ]
        ]
    ];
}

void SMaterialParentBatchPanel::InvalidatePreview()
{
    bPreviewReady = false;
    Plan = MaterialParentBatch::FPlan();
    Message = TEXT("设置或选择已变化，请重新生成预览。");
    for (const FEntryPtr& Entry : Entries) { Entry->Status.Reset(); }
    if (List) { List->RequestListRefresh(); }
}

void SMaterialParentBatchPanel::Refilter()
{
    InvalidatePreview();
    VisibleEntries.Reset();
    for (const FEntryPtr& Entry : Entries)
    {
        if ((!ParentFilter.IsValid() || Entry->Material->Parent == ParentFilter.Get()) &&
            (Search.IsEmpty() || Entry->Material->GetPathName().Contains(Search))) { VisibleEntries.Add(Entry); }
    }
    if (List) { List->RequestListRefresh(); }
}

FReply SMaterialParentBatchPanel::ReadAssets()
{
    TArray<FAssetData> Assets;
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Assets);
    TArray<UObject*> Objects;
    for (const FAssetData& Asset : Assets) { Objects.Add(Asset.GetAsset()); }
    Entries.Reset();
    MaterialParentBatch::CollectAssets(Objects, Entries);
    Refilter();
    Message = TEXT("已读取内容浏览器选择。支持静态模型、骨骼模型和材质实例；普通材质、空槽及其他资产自动忽略。");
    return FReply::Handled();
}

FReply SMaterialParentBatchPanel::ReadActors()
{
    TArray<AActor*> Actors;
    if (GEditor)
    {
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
        { if (AActor* Actor = Cast<AActor>(*It)) { Actors.Add(Actor); } }
    }
    Entries.Reset();
    MaterialParentBatch::CollectActors(Actors, Entries);
    Refilter();
    Message = TEXT("已读取关卡选中物体。读取其静态/骨骼网格组件的实际材质，包含组件覆盖。实例化网格组件会整体处理。");
    return FReply::Handled();
}

FReply SMaterialParentBatchPanel::Preview()
{
    FString Error;
    bPreviewReady = MaterialParentBatch::BuildPlan(VisibleEntries, NewParent.Get(), Mode, OutputFolder, Plan, Error);
    Message = bPreviewReady ? FString::Printf(TEXT("预览通过：%d 个材质实例。请检查清单和处理模式；执行后不自动保存，可 Ctrl+Z 撤销修改。"), Plan.Entries.Num()) : Error;
    List->RequestListRefresh();
    return FReply::Handled();
}

FReply SMaterialParentBatchPanel::Execute()
{
    if (!bPreviewReady) { return FReply::Handled(); }
    const FText Confirmation = FText::FromString(FString::Printf(TEXT("将处理 %d 个材质实例。\n%s\n\n执行后不会自动保存。继续？"),
        Plan.Entries.Num(), *ModeHelp().ToString()));
    if (FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) != EAppReturnType::Yes) { return FReply::Handled(); }
    const MaterialParentBatch::FResult Result = MaterialParentBatch::ExecutePlan(Plan);
    LastBackup = Result.BackupDirectory;
    bPreviewReady = false;
    Plan = MaterialParentBatch::FPlan();
    Message = FString::Printf(TEXT("完成 %d 项。%s\n检查效果后自行保存；Ctrl+Z 撤销父级与回填修改。新建副本可能仍保留在内容浏览器中。"),
        Result.Changed, Result.Error.IsEmpty() ? TEXT("未自动保存。") : *Result.Error);
    if (!LastBackup.IsEmpty()) { Message += TEXT("\n磁盘原件备份：") + LastBackup; }
    List->RequestListRefresh();
    return FReply::Handled();
}

FReply SMaterialParentBatchPanel::CheckVisible(bool bChecked)
{
    for (const FEntryPtr& Entry : VisibleEntries) { Entry->bChecked = bChecked; }
    InvalidatePreview();
    return FReply::Handled();
}

FText SMaterialParentBatchPanel::Summary() const
{
    int32 Checked = 0;
    for (const FEntryPtr& Entry : VisibleEntries) { Checked += Entry->bChecked ? 1 : 0; }
    return FText::FromString(FString::Printf(TEXT("共 %d 个 MI · 当前显示 %d · 勾选 %d"), Entries.Num(), VisibleEntries.Num(), Checked));
}

FText SMaterialParentBatchPanel::ModeHelp() const
{
    return Mode == MaterialParentBatch::EMode::ModifyOriginal
        ? LOCTEXT("OriginalHelp", "修改原实例：所有共用该 MI 的模型和场景都会变化，不限于当前选择。")
        : LOCTEXT("CopyHelp", "复制模式：关卡选择只修改所选物体的组件覆盖；内容浏览器选择修改所选模型资产的材质槽（该模型的所有摆放均受影响）。直接选 MI 时只创建副本。共用同一 MI 的所选目标共用一个副本。");
}

TSharedRef<ITableRow> SMaterialParentBatchPanel::GenerateRow(FEntryPtr Entry, const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<FEntryPtr>, Owner).Padding(6)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 10, 0)
        [ SNew(SCheckBox).IsChecked_Lambda([Entry] { return Entry->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Entry](ECheckBoxState State) { Entry->bChecked = State == ECheckBoxState::Checked; InvalidatePreview(); }) ]
        + SHorizontalBox::Slot().FillWidth(1)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(Entry->Material->GetPathName())).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text_Lambda([Entry] { return FText::FromString(TEXT("原父级：") + GetPathNameSafe(Entry->Material->Parent)); }) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(FString::Printf(TEXT("所选目标中的材质槽：%d"), Entry->Bindings.Num())))
                .ToolTipText_Lambda([Entry]
                { FString Text; for (const auto& Binding : Entry->Bindings) { Text += FString::Printf(TEXT("%s [槽 %d]\n"), *GetPathNameSafe(Binding.Owner.Get()), Binding.Slot); } return FText::FromString(Text); }) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).AutoWrapText(true).Text_Lambda([Entry] { return FText::FromString(Entry->Status); }) ]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
        [ SNew(SButton).Text(LOCTEXT("Locate", "定位"))
            .OnClicked_Lambda([Entry] { TArray<UObject*> Assets { Entry->Material.Get() }; GEditor->SyncBrowserToObjects(Assets); return FReply::Handled(); }) ]
    ];
}

#undef LOCTEXT_NAMESPACE
