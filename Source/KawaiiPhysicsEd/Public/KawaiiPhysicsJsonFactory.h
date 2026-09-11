// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "KawaiiPhysicsJsonFactory.generated.h"

/**
 * Lets an FModel JSON export be dragged straight into the Content Browser.
 *
 * The AnimGraph canvas cannot accept an OS file drop: SGraphPanel routes drops
 * through AssetUtil::ExtractAssetDataFromDrag, which only reads UE asset paths
 * marshalled as text and ignores FExternalDragOperation::GetFiles() entirely.
 * SGraphPanel is also a private widget, so a plugin cannot substitute its own.
 * The Content Browser, by contrast, natively runs factories on dropped files.
 *
 * Creates an Anim Blueprint from the export and fills it with the recovered
 * Kawaii Physics setup.
 */
UCLASS()
class UKawaiiPhysicsJsonFactory : public UFactory
{
	GENERATED_BODY()

public:
	UKawaiiPhysicsJsonFactory();

	// UFactory interface
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	                                   const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn,
	                                   bool& bOutOperationCanceled) override;
	virtual FText GetDisplayName() const override;
	// End of UFactory interface

private:
	/**
	 * Finds the skeleton the export was authored against.
	 *
	 * Tries the exact package path the game used first, then falls back to a
	 * name match anywhere in the project, since extracted assets often land
	 * somewhere else.
	 */
	static class USkeleton* ResolveSkeleton(const FString& JsonText, FString& OutWantedPath);
};
