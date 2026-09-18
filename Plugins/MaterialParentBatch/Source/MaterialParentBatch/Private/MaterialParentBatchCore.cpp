/**
 * 📌 MaterialParentBatchCore.cpp — 处理预览中的精确对象，不扫描或改写整个工程。
 * 参数策略：保留实例自己覆盖的同名同类型参数；继承值由新父级决定。
 * 数据保护：预检、磁盘原件备份、统一撤销事务、不自动保存、不覆盖已有副本。
 */
#include "MaterialParentBatchCore.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "MaterialShared.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace MaterialParentBatch
{
UMaterialInterface* FBinding::GetMaterial() const
{
    UObject* Object = Owner.Get();
    if (const UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
    {
        return Mesh->GetStaticMaterials().IsValidIndex(Slot) ? Mesh->GetMaterial(Slot) : nullptr;
    }
    if (const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
    {
        return Mesh->GetMaterials().IsValidIndex(Slot) ? Mesh->GetMaterials()[Slot].MaterialInterface.Get() : nullptr;
    }
    if (const UMeshComponent* Component = Cast<UMeshComponent>(Object))
    {
        return Slot >= 0 && Slot < Component->GetNumMaterials() ? Component->GetMaterial(Slot) : nullptr;
    }
    return nullptr;
}

void FBinding::Assign(UMaterialInterface* Material) const
{
    UObject* Object = Owner.Get();
    check(Object);
    Object->SetFlags(RF_Transactional);
    Object->Modify();
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
    {
        Mesh->SetMaterial(Slot, Material);
    }
    else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
    {
        TArray<FSkeletalMaterial> Materials = SkeletalMesh->GetMaterials();
        Materials[Slot].MaterialInterface = Material;
        FProperty* Property = FindFProperty<FProperty>(USkeletalMesh::StaticClass(), TEXT("Materials"));
        SkeletalMesh->PreEditChange(Property);
        SkeletalMesh->SetMaterials(Materials);
        FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
        SkeletalMesh->PostEditChangeProperty(Event);
    }
    else if (UMeshComponent* Component = Cast<UMeshComponent>(Object))
    {
        if (AActor* Actor = Component->GetOwner()) { Actor->Modify(); }
        Component->SetMaterial(Slot, Material);
        Component->MarkRenderStateDirty();
    }
    Object->MarkPackageDirty();
}

namespace
{
void AddMaterial(UMaterialInterface* Material, UObject* Owner, int32 Slot, TArray<TSharedPtr<FEntry>>& Entries)
{
    UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Material);
    if (!Instance) { return; }
    TSharedPtr<FEntry>* Found = Entries.FindByPredicate([Instance](const TSharedPtr<FEntry>& Row)
    { return Row->Material.Get() == Instance; });
    TSharedPtr<FEntry> Entry;
    if (Found) { Entry = *Found; }
    else { Entry = MakeShared<FEntry>(Instance); Entries.Add(Entry); }
    if (Owner && !Entry->Bindings.ContainsByPredicate([Owner, Slot](const FBinding& Binding)
        { return Binding.Owner.Get() == Owner && Binding.Slot == Slot; }))
    {
        Entry->Bindings.Add({Owner, Slot});
    }
}

/** 📌 使用材质编辑器同款代理；父级变化包含静态参数和组件渲染状态更新。 */
void SetParentViaEditor(UMaterialInstanceConstant* Instance, UMaterialInterface* Parent)
{
    TStrongObjectPtr<UMaterialEditorInstanceConstant> Proxy(NewObject<UMaterialEditorInstanceConstant>());
    Proxy->SetSourceInstance(Instance);
    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Proxy->GetClass(), TEXT("Parent"));
    check(Property);
    Proxy->PreEditChange(Property);
    Property->SetObjectPropertyValue_InContainer(Proxy.Get(), Parent);
    FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
    Proxy->PostEditChangeProperty(Event);
}

void RestoreOverrides(UMaterialInstanceConstant* Instance, const TArray<FOverride>& Overrides)
{
    FMaterialUpdateContext RenderUpdate;
    RenderUpdate.AddMaterialInstance(Instance);
    {
        FMaterialInstanceParameterUpdateContext ParameterUpdate(Instance);
        for (const FOverride& Override : Overrides)
        {
            FMaterialParameterMetadata Metadata = Override.Metadata;
            // 📌 新母材质的表达式 GUID 可能不同，按参数信息重新取得当前 GUID。
            FMaterialParameterMetadata Current;
            if (Instance->Parent && Instance->Parent->GetParameterValue(Metadata.Value.Type, Override.Info, Current))
            {
                Metadata.ExpressionGuid = Current.ExpressionGuid;
            }
            ParameterUpdate.SetParameterValueEditorOnly(Override.Info, Metadata);
        }
    }
    Instance->PostEditChange();
}

FString ValidateBindings(const FEntry& Entry)
{
    for (const FBinding& Binding : Entry.Bindings)
    {
        if (!Binding.Owner.IsValid() || Binding.GetMaterial() != Entry.Material.Get())
        {
            return TEXT("模型或材质槽已变化，请重新读取选择并预览。");
        }
        if (Binding.Owner->IsAsset() && !Binding.Owner->GetPackage()->GetName().StartsWith(TEXT("/Game/")))
        {
            return TEXT("不能回填引擎或外部插件模型，请先复制模型到工程内容目录。");
        }
        if (const UMeshComponent* Component = Cast<UMeshComponent>(Binding.Owner.Get()))
        {
            if (!Component->GetWorld() || Component->GetWorld()->WorldType != EWorldType::Editor)
            {
                return TEXT("只支持编辑器关卡中的网格组件，不能修改运行中或预览世界。");
            }
        }
    }
    return FString();
}

/** 📌 备份磁盘上的原始包；未保存内存状态由编辑器撤销事务保护，不强制保存用户工作。 */
bool BackupPackages(const FPlan& Plan, FString& OutDirectory, FString& Error)
{
    OutDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MaterialParentBatch/Backups") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-")) + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    if (!IFileManager::Get().MakeDirectory(*OutDirectory, true))
    { Error = TEXT("无法创建备份目录，已取消执行。"); return false; }
    TSet<UPackage*> Packages;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FPlannedEntry& Item : Plan.Entries)
    {
        Packages.Add(Item.Entry->Material->GetPackage());
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("source"), Item.Entry->Material->GetPathName());
        Row->SetStringField(TEXT("previousParent"), GetPathNameSafe(Item.PreviousParent.Get()));
        Row->SetStringField(TEXT("newParent"), Plan.NewParent->GetPathName());
        Row->SetStringField(TEXT("outputPackage"), Item.OutputPackage);
        TArray<TSharedPtr<FJsonValue>> Bindings;
        for (const FBinding& Binding : Item.Entry->Bindings)
        {
            TSharedRef<FJsonObject> BindingJson = MakeShared<FJsonObject>();
            BindingJson->SetStringField(TEXT("owner"), Binding.Owner->GetPathName());
            BindingJson->SetNumberField(TEXT("slot"), Binding.Slot);
            Bindings.Add(MakeShared<FJsonValueObject>(BindingJson));
            if (Plan.Mode == EMode::DuplicateAndAssign) { Packages.Add(Binding.Owner->GetPackage()); }
        }
        Row->SetArrayField(TEXT("bindings"), Bindings);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    TArray<TSharedPtr<FJsonValue>> PackageRows;
    for (UPackage* Package : Packages)
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Package->GetName());
        Row->SetBoolField(TEXT("hadUnsavedChanges"), Package->IsDirty());
        FString Filename;
        if (FPackageName::DoesPackageExist(Package->GetName(), &Filename))
        {
            const FString SubPath = Package->GetName().RightChop(1);
            const FString DestinationBase = OutDirectory / SubPath;
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestinationBase), true);
            // 📌 同时保留可能存在的分离导出与 bulk 数据文件。
            for (const FString& Extension : {FPaths::GetExtension(Filename, true), FString(TEXT(".uexp")), FString(TEXT(".ubulk"))})
            {
                const FString Source = FPaths::ChangeExtension(Filename, Extension);
                if (IFileManager::Get().FileExists(*Source) &&
                    IFileManager::Get().Copy(*(DestinationBase + Extension), *Source, false) != COPY_OK)
                { Error = TEXT("备份原文件失败，已取消执行：") + Source; return false; }
            }
            Row->SetStringField(TEXT("originalFile"), FPaths::ConvertRelativePathToFull(Filename));
            Row->SetStringField(TEXT("backupFile"), DestinationBase + FPaths::GetExtension(Filename, true));
        }
        PackageRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("mode"), Plan.Mode == EMode::ModifyOriginal ? TEXT("modifyOriginal") : TEXT("duplicateAndAssign"));
    Root->SetArrayField(TEXT("entries"), Rows);
    Root->SetArrayField(TEXT("packages"), PackageRows);
    Root->SetStringField(TEXT("note"), TEXT("Backups contain last-saved disk packages. Use editor Undo for pre-operation unsaved state. No assets were auto-saved."));
    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (!FJsonSerializer::Serialize(Root, Writer) || !FFileHelper::SaveStringToFile(Json, *(OutDirectory / TEXT("manifest.json"))))
    { Error = TEXT("无法写入备份记录，已取消执行。"); return false; }
    return true;
}
}

void CollectAssets(const TArray<UObject*>& Objects, TArray<TSharedPtr<FEntry>>& OutEntries)
{
    for (UObject* Object : Objects)
    {
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
        {
            for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
            { AddMaterial(Mesh->GetMaterial(Slot), Mesh, Slot, OutEntries); }
        }
        else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
        {
            for (int32 Slot = 0; Slot < SkeletalMesh->GetMaterials().Num(); ++Slot)
            { AddMaterial(SkeletalMesh->GetMaterials()[Slot].MaterialInterface, SkeletalMesh, Slot, OutEntries); }
        }
        else { AddMaterial(Cast<UMaterialInterface>(Object), nullptr, INDEX_NONE, OutEntries); }
    }
}

void CollectActors(const TArray<AActor*>& Actors, TArray<TSharedPtr<FEntry>>& OutEntries)
{
    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor)) { continue; }
        TInlineComponentArray<UMeshComponent*> Components(Actor);
        for (UMeshComponent* Component : Components)
        {
            if (!Component->IsA<UStaticMeshComponent>() && !Component->IsA<USkeletalMeshComponent>()) { continue; }
            for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
            { AddMaterial(Component->GetMaterial(Slot), Component, Slot, OutEntries); }
        }
    }
}

TArray<FOverride> CaptureOverrides(UMaterialInstanceConstant* Instance)
{
    TArray<FOverride> Result;
    for (int32 TypeIndex = 0; TypeIndex < NumMaterialParameterTypes; ++TypeIndex)
    {
        TMap<FMaterialParameterInfo, FMaterialParameterMetadata> Values;
        Instance->GetAllParametersOfType(static_cast<EMaterialParameterType>(TypeIndex), Values);
        for (const auto& Pair : Values)
        {
            if (Pair.Value.bOverride) { Result.Add({Pair.Key, Pair.Value}); }
        }
    }
    return Result;
}

bool SameOverrides(const TArray<FOverride>& A, const TArray<FOverride>& B)
{
    if (A.Num() != B.Num()) { return false; }
    for (const FOverride& Value : A)
    {
        if (!B.ContainsByPredicate([&Value](const FOverride& Other)
            { return Value.Info == Other.Info && Value.Metadata.Value == Other.Metadata.Value; })) { return false; }
    }
    return true;
}

FString ValidateParent(UMaterialInstanceConstant* Instance, UMaterialInterface* NewParent)
{
    if (!Instance || !NewParent) { return TEXT("请选择材质实例和新母材质。"); }
    if (Instance->Parent == NewParent) { return TEXT("当前父级已是目标，无需替换。"); }
    TSet<UMaterialInterface*> Visited;
    for (UMaterialInterface* Current = NewParent; Current;)
    {
        if (Current == Instance || Visited.Contains(Current)) { return TEXT("此操作会形成循环父级引用。"); }
        Visited.Add(Current);
        UMaterialInstance* ParentInstance = Cast<UMaterialInstance>(Current);
        Current = ParentInstance ? ParentInstance->Parent.Get() : nullptr;
    }
    FMaterialLayersFunctions Layers;
    if ((Instance->GetMaterialLayers(Layers) && Layers.Layers.Num() > 0) ||
        (NewParent->GetMaterialLayers(Layers) && Layers.Layers.Num() > 0))
    { return TEXT("此版本不迁移 Material Layers 层堆栈，请使用材质实例编辑器处理。"); }
    FString InvalidParameters;
    for (const FOverride& Override : CaptureOverrides(Instance))
    {
        FMaterialParameterMetadata Target;
        if (Override.Info.Association != EMaterialParameterAssociation::GlobalParameter ||
            !NewParent->GetParameterValue(Override.Metadata.Value.Type, Override.Info, Target) ||
            Override.Metadata.bUsedAsAtlasPosition || Target.bUsedAsAtlasPosition)
        {
            if (!InvalidParameters.IsEmpty()) { InvalidParameters += TEXT("、"); }
            InvalidParameters += Override.Info.Name.ToString();
        }
    }
    return InvalidParameters.IsEmpty() ? FString() : TEXT("以下覆盖参数缺失、类型不同或暂不支持：") + InvalidParameters;
}

bool ChangeParent(UMaterialInstanceConstant* Instance, UMaterialInterface* NewParent, FString& Error)
{
    Error = ValidateParent(Instance, NewParent);
    if (!Error.IsEmpty()) { return false; }
    TStrongObjectPtr<UMaterialInterface> OldParent(Instance->Parent);
    const TArray<FOverride> Before = CaptureOverrides(Instance);
    Instance->SetFlags(RF_Transactional);
    Instance->Modify();
    SetParentViaEditor(Instance, NewParent);
    RestoreOverrides(Instance, Before);
    if (Instance->Parent != NewParent || !SameOverrides(Before, CaptureOverrides(Instance)))
    {
        // 📌 发生意外参数丢失时立即恢复原父级和覆盖值，不继续回填模型。
        SetParentViaEditor(Instance, OldParent.Get());
        RestoreOverrides(Instance, Before);
        Error = TEXT("替换后的参数校验失败，已恢复原父级和参数。");
        return false;
    }
    Instance->MarkPackageDirty();
    return true;
}

bool BuildPlan(const TArray<TSharedPtr<FEntry>>& Entries, UMaterialInterface* NewParent,
    EMode Mode, const FString& OutputFolder, FPlan& OutPlan, FString& Error)
{
    OutPlan = FPlan();
    Error.Reset();
    if (!NewParent || !NewParent->IsAsset()) { Error = TEXT("请选择已存在的新母材质资产。"); return false; }
    if (Mode == EMode::DuplicateAndAssign &&
        (!OutputFolder.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(OutputFolder)))
    { Error = TEXT("副本目录应是 /Game/ 下的有效内容路径，例如 /Game/MaterialParentBatch/Variants。"); return false; }
    OutPlan.NewParent.Reset(NewParent);
    OutPlan.Mode = Mode;
    TSet<FString> ReservedPackages;
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    for (const TSharedPtr<FEntry>& Entry : Entries)
    {
        Entry->Status.Reset();
        if (!Entry->bChecked) { continue; }
        UMaterialInstanceConstant* Instance = Entry->Material.Get();
        FString Problem = ValidateParent(Instance, NewParent);
        if (Problem.IsEmpty() && !Instance->GetPackage()->GetName().StartsWith(TEXT("/Game/")))
        { Problem = TEXT("仅处理工程 /Game/ 内的材质实例，不改引擎或外部插件内容。"); }
        if (Problem.IsEmpty()) { Problem = ValidateBindings(*Entry); }
        if (!Problem.IsEmpty())
        {
            Entry->Status = Problem;
            Error += Instance->GetName() + TEXT(": ") + Problem + TEXT("\n");
            continue;
        }
        FPlannedEntry Item;
        Item.Entry = Entry;
        Item.PreviousParent.Reset(Instance->Parent);
        Item.Overrides = CaptureOverrides(Instance);
        if (Mode == EMode::DuplicateAndAssign)
        {
            int32 Collision = 0;
            do
            {
                const FString Suffix = Collision == 0 ? TEXT("_Variant") : FString::Printf(TEXT("_Variant%d"), Collision);
                AssetTools.CreateUniqueAssetName(OutputFolder / Instance->GetName(), Suffix, Item.OutputPackage, Item.OutputName);
                ++Collision;
            } while (ReservedPackages.Contains(Item.OutputPackage));
            ReservedPackages.Add(Item.OutputPackage);
            Entry->Status = Item.OutputPackage + (Entry->Bindings.IsEmpty() ? TEXT("（仅生成副本，不自动回填）") : TEXT("（回填所选目标）"));
        }
        else { Entry->Status = TEXT("修改原实例：所有引用者都会受到影响"); }
        OutPlan.Entries.Add(MoveTemp(Item));
    }
    if (OutPlan.Entries.IsEmpty() && Error.IsEmpty()) { Error = TEXT("当前没有勾选可处理的材质实例。"); }
    if (!Error.IsEmpty()) { OutPlan.Entries.Reset(); return false; }
    return true;
}

FResult ExecutePlan(const FPlan& Plan)
{
    FResult Result;
    if (!GEditor || GEditor->PlayWorld || !Plan.NewParent.IsValid() || Plan.Entries.IsEmpty())
    { Result.Error = TEXT("请停止运行/模拟，读取选择并生成有效预览。"); return Result; }
    // 📌 在任何写操作之前完整检查所有目标，避免过期清单造成错改。
    for (const FPlannedEntry& Item : Plan.Entries)
    {
        UMaterialInstanceConstant* Instance = Item.Entry->Material.Get();
        Result.Error = ValidateParent(Instance, Plan.NewParent.Get());
        if (Result.Error.IsEmpty()) { Result.Error = ValidateBindings(*Item.Entry); }
        if (Result.Error.IsEmpty() && (Instance->Parent != Item.PreviousParent.Get() ||
            !SameOverrides(Item.Overrides, CaptureOverrides(Instance))))
        { Result.Error = TEXT("预览后材质已发生变化，请重新预览。"); }
        if (Result.Error.IsEmpty() && Plan.Mode == EMode::DuplicateAndAssign &&
            (FPackageName::DoesPackageExist(Item.OutputPackage) || FindPackage(nullptr, *Item.OutputPackage)))
        { Result.Error = TEXT("副本名称已被占用，请重新预览。"); }
        if (!Result.Error.IsEmpty()) { return Result; }
    }
    if (!BackupPackages(Plan, Result.BackupDirectory, Result.Error)) { return Result; }
    const FScopedTransaction Transaction(NSLOCTEXT("MaterialParentBatch", "UndoBatch", "批量更换材质母材质"));
    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    for (const FPlannedEntry& Item : Plan.Entries)
    {
        UMaterialInstanceConstant* Instance = Item.Entry->Material.Get();
        if (Plan.Mode == EMode::DuplicateAndAssign)
        {
            Instance = Cast<UMaterialInstanceConstant>(AssetTools.DuplicateAsset(Item.OutputName,
                FPackageName::GetLongPackagePath(Item.OutputPackage), Instance));
            if (!Instance) { Result.Error = TEXT("创建副本失败，已停止。此前成功项目可统一撤销。"); break; }
        }
        if (!ChangeParent(Instance, Plan.NewParent.Get(), Result.Error)) { break; }
        if (Plan.Mode == EMode::DuplicateAndAssign)
        {
            for (const FBinding& Binding : Item.Entry->Bindings) { Binding.Assign(Instance); }
        }
        Item.Entry->Status = TEXT("已完成，尚未保存");
        Result.Outputs.Add(Instance);
        ++Result.Changed;
    }
    GEditor->RedrawAllViewports();
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("Completed: %d\nError: %s\n"), Result.Changed, *Result.Error),
        *(Result.BackupDirectory / TEXT("result.txt")));
    return Result;
}
}
