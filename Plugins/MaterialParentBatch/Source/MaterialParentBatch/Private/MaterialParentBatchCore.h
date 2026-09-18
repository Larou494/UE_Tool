/**
 * 📌 MaterialParentBatchCore — 编辑器内材质收集、预检与安全换父级。
 * 输入：模型资产、材质实例或关卡 Actor。输出：去重清单与明确的回填槽位。
 * 不自动保存资产；替换采用材质实例编辑器的更新路径，避免 UE-269068。
 */
#pragma once

#include "CoreMinimal.h"
// 📌 UE 5.7/5.8 使用新的参数头文件，5.5/5.6 保留原有入口。
#if __has_include("Materials/MaterialParameters.h")
#include "Materials/MaterialParameters.h"
#else
#include "MaterialTypes.h"
#endif
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/StrongObjectPtr.h"

class AActor;

namespace MaterialParentBatch
{
enum class EMode : uint8 { ModifyOriginal, DuplicateAndAssign };

/** 📌 Owner 可以是静态/骨骼模型资产，或关卡中对应的网格组件。 */
struct FBinding
{
    TWeakObjectPtr<UObject> Owner;
    int32 Slot = INDEX_NONE;
    UMaterialInterface* GetMaterial() const;
    void Assign(UMaterialInterface* Material) const;
};

struct FEntry
{
    explicit FEntry(UMaterialInstanceConstant* InMaterial) : Material(InMaterial) {}
    TStrongObjectPtr<UMaterialInstanceConstant> Material;
    TArray<FBinding> Bindings;
    bool bChecked = true;
    FString Status;
};

struct FOverride
{
    FMaterialParameterInfo Info;
    FMaterialParameterMetadata Metadata;
};

struct FPlannedEntry
{
    TSharedPtr<FEntry> Entry;
    TStrongObjectPtr<UMaterialInterface> PreviousParent;
    TArray<FOverride> Overrides;
    FString OutputPackage;
    FString OutputName;
};

/** 📌 预览冻结处理对象；执行前再次校验父级、参数和槽位，拒绝过期预览。 */
struct FPlan
{
    TStrongObjectPtr<UMaterialInterface> NewParent;
    EMode Mode = EMode::DuplicateAndAssign;
    TArray<FPlannedEntry> Entries;
};

struct FResult
{
    int32 Changed = 0;
    FString Error;
    FString BackupDirectory;
    TArray<TWeakObjectPtr<UMaterialInstanceConstant>> Outputs;
};

void CollectAssets(const TArray<UObject*>& Objects, TArray<TSharedPtr<FEntry>>& OutEntries);
void CollectActors(const TArray<AActor*>& Actors, TArray<TSharedPtr<FEntry>>& OutEntries);
TArray<FOverride> CaptureOverrides(UMaterialInstanceConstant* Instance);
bool SameOverrides(const TArray<FOverride>& A, const TArray<FOverride>& B);
FString ValidateParent(UMaterialInstanceConstant* Instance, UMaterialInterface* NewParent);
bool ChangeParent(UMaterialInstanceConstant* Instance, UMaterialInterface* NewParent, FString& Error);
bool BuildPlan(const TArray<TSharedPtr<FEntry>>& Entries, UMaterialInterface* NewParent,
    EMode Mode, const FString& OutputFolder, FPlan& OutPlan, FString& Error);
FResult ExecutePlan(const FPlan& Plan);
}
