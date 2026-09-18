/** 📌 MaterialParentBatchTests — 在隔离的临时资产上验证换父级、参数保护、槽位收集与过期预览。 */
#if WITH_DEV_AUTOMATION_TESTS
#include "MaterialParentBatchCore.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace MaterialParentBatchTests
{
using namespace MaterialParentBatch;

template<typename T>
T* NewAsset(const TCHAR* Name)
{
    const FString PackageName = FString(TEXT("/Game/MaterialParentBatchTests/")) + Name + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    return NewObject<T>(CreatePackage(*PackageName), Name, RF_Public | RF_Standalone | RF_Transactional);
}

UMaterial* MakeParent(const TCHAR* Name, bool bWithScalar = true)
{
    UMaterial* Material = NewAsset<UMaterial>(Name);
    if (bWithScalar)
    {
        auto* Scalar = CastChecked<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass()));
        Scalar->ParameterName = TEXT("Amount");
        Scalar->DefaultValue = 0.2f;
    }
    auto* Vector = CastChecked<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass()));
    Vector->ParameterName = TEXT("Tint");
    auto* Texture = CastChecked<UMaterialExpressionTextureObjectParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureObjectParameter::StaticClass()));
    Texture->ParameterName = TEXT("SurfaceTexture");
    Texture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
    auto* Switch = CastChecked<UMaterialExpressionStaticBoolParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionStaticBoolParameter::StaticClass()));
    Switch->ParameterName = TEXT("UseDetail");
    Material->PostEditChange();
    return Material;
}

UMaterialInstanceConstant* MakeInstance(UMaterialInterface* Parent)
{
    auto* Instance = NewAsset<UMaterialInstanceConstant>(TEXT("MI_Test"));
    FString Error;
    ChangeParent(Instance, Parent, Error);
    return Instance;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialBatchParentTest, "MaterialParentBatch.ParentAndParameters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMaterialBatchParentTest::RunTest(const FString& Parameters)
{
    using namespace MaterialParentBatch;
    using namespace MaterialParentBatchTests;
    UMaterial* A = MakeParent(TEXT("M_A"));
    UMaterial* B = MakeParent(TEXT("M_B"));
    UMaterial* Missing = MakeParent(TEXT("M_Missing"), false);
    UMaterialInstanceConstant* Instance = MakeInstance(A);
    TestTrue(TEXT("Initial parent"), Instance->Parent == A);
    Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Amount")), 0.73f);
    Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Tint")), FLinearColor(0.1f, 0.2f, 0.8f, 1.f));
    UTexture* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
    Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("SurfaceTexture")), Texture);
    {
        FMaterialInstanceParameterUpdateContext Update(Instance);
        FMaterialParameterMetadata Value(FMaterialParameterValue(true));
        Value.bOverride = true;
        Update.SetParameterValueEditorOnly(FMaterialParameterInfo(TEXT("UseDetail")), Value);
    }
    Instance->PostEditChange();
    const TArray<FOverride> Before = CaptureOverrides(Instance);
    TestEqual(TEXT("Scalar, vector, texture and static overrides captured"), Before.Num(), 4);
    TestFalse(TEXT("Missing parameter is rejected"), ValidateParent(Instance, Missing).IsEmpty());
    TestFalse(TEXT("Self parent is rejected"), ValidateParent(Instance, Instance).IsEmpty());
    UMaterialInstanceConstant* Child = MakeInstance(Instance);
    TestFalse(TEXT("Descendant parent is rejected"), ValidateParent(Instance, Child).IsEmpty());
    FString Error;
    TestTrue(TEXT("Parent change succeeds"), ChangeParent(Instance, B, Error));
    TestTrue(TEXT("Parent becomes B"), Instance->Parent == B);
    TestTrue(TEXT("Every explicit override retained"), SameOverrides(Before, CaptureOverrides(Instance)));
    TestFalse(TEXT("Same-parent no-op is rejected"), ValidateParent(Instance, B).IsEmpty());
    for (int32 Iteration = 0; Iteration < 8; ++Iteration)
    {
        TestTrue(TEXT("Repeated editor-path swaps"), ChangeParent(Instance, (Iteration % 2 == 0) ? A : B, Error));
    }
    TestTrue(TEXT("Repeated swaps retain overrides"), SameOverrides(Before, CaptureOverrides(Instance)));
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Test material parent undo")));
        TestTrue(TEXT("Transactional parent change"), ChangeParent(Instance, A, Error));
    }
    TestTrue(TEXT("Undo is available"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores parent"), Instance->Parent == B);
    TestTrue(TEXT("Undo restores parameter overrides"), SameOverrides(Before, CaptureOverrides(Instance)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialBatchCollectionTest, "MaterialParentBatch.CollectionAndAssignment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMaterialBatchCollectionTest::RunTest(const FString& Parameters)
{
    using namespace MaterialParentBatch;
    using namespace MaterialParentBatchTests;
    UMaterial* Parent = MakeParent(TEXT("M_Collect"));
    auto* Instance = MakeInstance(Parent);
    auto* Other = MakeInstance(Parent);
    UStaticMesh* Mesh = NewAsset<UStaticMesh>(TEXT("SM_Test"));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Instance));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Instance));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Parent));
    USkeletalMesh* Skeletal = NewAsset<USkeletalMesh>(TEXT("SK_Test"));
    Skeletal->SetMaterials({FSkeletalMaterial(Instance)});
    TArray<TSharedPtr<FEntry>> Entries;
    CollectAssets({Mesh, Skeletal, Instance, Mesh, Parent}, Entries);
    TestEqual(TEXT("Deduplicate across slots and asset kinds"), Entries.Num(), 1);
    TestEqual(TEXT("All distinct static and skeletal slots collected"), Entries[0]->Bindings.Num(), 3);
    UStaticMeshComponent* Selected = NewObject<UStaticMeshComponent>();
    UStaticMeshComponent* Unselected = NewObject<UStaticMeshComponent>();
    Selected->SetStaticMesh(Mesh);
    Unselected->SetStaticMesh(Mesh);
    const FBinding Binding {Selected, 0};
    Binding.Assign(Other);
    TestTrue(TEXT("Selected component changed"), Selected->GetMaterial(0) == Other);
    TestTrue(TEXT("Unselected component unchanged"), Unselected->GetMaterial(0) == Instance);
    TestTrue(TEXT("Mesh asset unchanged by component assignment"), Mesh->GetMaterial(0) == Instance);
    TestTrue(TEXT("Second slot unchanged"), Selected->GetMaterial(1) == Instance);
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Test component override undo")));
        Binding.Assign(Instance);
    }
    TestTrue(TEXT("Component change undo available"), GEditor->UndoTransaction());
    TestTrue(TEXT("Undo restores component override"), Selected->GetMaterial(0) == Other);
    const FBinding SkeletalBinding {Skeletal, 0};
    SkeletalBinding.Assign(Other);
    TestTrue(TEXT("Skeletal slot assignment"), Skeletal->GetMaterials()[0].MaterialInterface == Other);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialBatchPreviewTest, "MaterialParentBatch.PreviewAndStaleGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMaterialBatchPreviewTest::RunTest(const FString& Parameters)
{
    using namespace MaterialParentBatch;
    using namespace MaterialParentBatchTests;
    UMaterial* A = MakeParent(TEXT("M_PreviewA"));
    UMaterial* B = MakeParent(TEXT("M_PreviewB"));
    auto* Instance = MakeInstance(A);
    TArray<TSharedPtr<FEntry>> Entries;
    CollectAssets({Instance}, Entries);
    FPlan Plan;
    FString Error;
    TestTrue(TEXT("MI-only duplicate plan accepted"), BuildPlan(Entries, B, EMode::DuplicateAndAssign, TEXT("/Game/MaterialParentBatchTests/Output"), Plan, Error));
    TestTrue(TEXT("Preview does not change parent"), Instance->Parent == A);
    TestFalse(TEXT("Preview has not created a package"), FindPackage(nullptr, *Plan.Entries[0].OutputPackage) != nullptr);
    Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Amount")), 0.91f);
    const FResult Stale = ExecutePlan(Plan);
    TestEqual(TEXT("Stale preview changes nothing"), Stale.Changed, 0);
    TestFalse(TEXT("Stale preview gives error"), Stale.Error.IsEmpty());
    TestTrue(TEXT("Stale preview retains parent"), Instance->Parent == A);
    TestFalse(TEXT("Engine output directory rejected"), BuildPlan(Entries, B, EMode::DuplicateAndAssign, TEXT("/Engine/Variants"), Plan, Error));
    Entries[0]->bChecked = false;
    TestFalse(TEXT("Unchecked entries are not processed"), BuildPlan(Entries, B, EMode::ModifyOriginal, FString(), Plan, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialBatchDuplicateTest, "MaterialParentBatch.DuplicateExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMaterialBatchDuplicateTest::RunTest(const FString& Parameters)
{
    using namespace MaterialParentBatch;
    using namespace MaterialParentBatchTests;
    UMaterial* A = MakeParent(TEXT("M_CopyA"));
    UMaterial* B = MakeParent(TEXT("M_CopyB"));
    auto* Original = MakeInstance(A);
    Original->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Amount")), 0.42f);
    const auto Before = CaptureOverrides(Original);
    TArray<TSharedPtr<FEntry>> Entries;
    CollectAssets({Original}, Entries);
    FPlan Plan;
    FString Error;
    TestTrue(TEXT("Build plan"), BuildPlan(Entries, B, EMode::DuplicateAndAssign, TEXT("/Game/MaterialParentBatchTests/Output"), Plan, Error));
    const FResult Result = ExecutePlan(Plan);
    TestEqual(TEXT("One copy created"), Result.Changed, 1);
    TestTrue(TEXT("No execution error"), Result.Error.IsEmpty());
    TestTrue(TEXT("Original parent unchanged"), Original->Parent == A);
    TestTrue(TEXT("Original overrides unchanged"), SameOverrides(Before, CaptureOverrides(Original)));
    if (Result.Outputs.Num() == 1 && Result.Outputs[0].IsValid())
    {
        TestTrue(TEXT("Copy is a different object"), Result.Outputs[0].Get() != Original);
        TestTrue(TEXT("Copy parent is B"), Result.Outputs[0]->Parent == B);
        TestTrue(TEXT("Copy retains overrides"), SameOverrides(Before, CaptureOverrides(Result.Outputs[0].Get())));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialBatchActorTest, "MaterialParentBatch.ActorCopyAndUndo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMaterialBatchActorTest::RunTest(const FString& Parameters)
{
    using namespace MaterialParentBatch;
    using namespace MaterialParentBatchTests;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!TestNotNull(TEXT("Editor world"), World)) { return false; }
    auto* A = MakeParent(TEXT("M_ActorA"));
    auto* B = MakeParent(TEXT("M_ActorB"));
    auto* Instance = MakeInstance(A);
    auto* Mesh = NewAsset<UStaticMesh>(TEXT("SM_ActorTest"));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Instance));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Instance));
    AActor* Actor = World->SpawnActor<AActor>();
    UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor, NAME_None, RF_Transactional);
    Actor->AddInstanceComponent(Component);
    Component->SetStaticMesh(Mesh);
    Component->RegisterComponent();
    TArray<TSharedPtr<FEntry>> Entries;
    CollectActors({Actor}, Entries);
    TestEqual(TEXT("Actor material deduplicated"), Entries.Num(), 1);
    if (Entries.Num() == 1)
    {
        TestEqual(TEXT("Actor slots retained"), Entries[0]->Bindings.Num(), 2);
        FPlan Plan;
        FString Error;
        TestTrue(TEXT("Actor copy preview"), BuildPlan(Entries, B, EMode::DuplicateAndAssign, TEXT("/Game/MaterialParentBatchTests/Output"), Plan, Error));
        const FResult Result = ExecutePlan(Plan);
        TestEqual(TEXT("Shared slots create one copy"), Result.Changed, 1);
        TestTrue(TEXT("Source MI untouched"), Instance->Parent == A);
        TestTrue(TEXT("Source mesh untouched"), Mesh->GetMaterial(0) == Instance);
        TestTrue(TEXT("Actor now uses new MI"), Component->GetMaterial(0) != Instance);
        TestTrue(TEXT("Both slots use the same copy"), Component->GetMaterial(0) == Component->GetMaterial(1));
        TestTrue(TEXT("Batch undo available"), GEditor->UndoTransaction());
        TestTrue(TEXT("Undo restores first slot"), Component->GetMaterial(0) == Instance);
        TestTrue(TEXT("Undo restores second slot"), Component->GetMaterial(1) == Instance);
    }
    World->DestroyActor(Actor);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaterialBatchPanelTest, "MaterialParentBatch.EditorPanel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMaterialBatchPanelTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("MaterialParentBatch")));
    TestTrue(TEXT("Plugin tab is registered and Slate panel constructs"), Tab.IsValid());
    if (Tab.IsValid()) { Tab->RequestCloseTab(); }
    return true;
}
#endif
