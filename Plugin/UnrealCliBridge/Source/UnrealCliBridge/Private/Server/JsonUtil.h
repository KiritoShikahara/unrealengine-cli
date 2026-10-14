#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace UCli
{
	inline TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Obj))
		{
			return nullptr;
		}
		return Obj;
	}

	inline TSharedPtr<FJsonValue> ParseValue(const FString& Text)
	{
		TSharedPtr<FJsonValue> Value;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Value);
		return Value;
	}

	inline FString ToJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	inline FString ToJson(const TSharedPtr<FJsonValue>& Value)
	{
		FString Out;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Out;
	}

	inline TSharedPtr<FJsonValue> Str(const FString& S) { return MakeShared<FJsonValueString>(S); }
	inline TSharedPtr<FJsonValue> Num(double N) { return MakeShared<FJsonValueNumber>(N); }
	inline TSharedPtr<FJsonValue> Bool(bool B) { return MakeShared<FJsonValueBoolean>(B); }
	inline TSharedPtr<FJsonValue> Obj(const TSharedPtr<FJsonObject>& O) { return MakeShared<FJsonValueObject>(O); }
	inline TSharedPtr<FJsonValue> Arr(const TArray<TSharedPtr<FJsonValue>>& A) { return MakeShared<FJsonValueArray>(A); }

	inline TArray<TSharedPtr<FJsonValue>> StrArray(const TArray<FString>& Items)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : Items)
		{
			Out.Add(Str(S));
		}
		return Out;
	}
}
