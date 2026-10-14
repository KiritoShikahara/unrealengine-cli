#pragma once

#include "AssetToolsModule.h"
#include "Factories/Factory.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"

namespace UCli
{
	/** Creates an asset of Class at a long package path using the first factory that can create it. */
	inline UObject* CreateAssetOfClass(const FString& Path, UClass* Class, FString& OutError, TFunctionRef<void(UFactory*)> Configure = [](UFactory*) {})
	{
		if (FPackageName::DoesPackageExist(Path) || FindObject<UPackage>(nullptr, *Path))
		{
			OutError = FString::Printf(TEXT("'%s' already exists"), *Path);
			return nullptr;
		}
		UFactory* Factory = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(UFactory::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
			{
				UFactory* CDO = It->GetDefaultObject<UFactory>();
				if (CDO->SupportedClass == Class && CDO->CanCreateNew())
				{
					Factory = NewObject<UFactory>(GetTransientPackage(), *It);
					break;
				}
			}
		}
		if (!Factory)
		{
			OutError = FString::Printf(TEXT("no factory creates %s"), *Class->GetName());
			return nullptr;
		}
		Configure(Factory);
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UObject* Asset = Tools.CreateAsset(FPackageName::GetShortName(Path), FPackageName::GetLongPackagePath(Path), Class, Factory);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("could not create %s at '%s'"), *Class->GetName(), *Path);
		}
		return Asset;
	}
}
