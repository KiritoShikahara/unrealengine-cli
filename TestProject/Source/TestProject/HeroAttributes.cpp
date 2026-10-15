#include "HeroAttributes.h"
#include "Net/UnrealNetwork.h"

UHeroAttributes::UHeroAttributes()
{
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitMana(50.f);
}

void UHeroAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UHeroAttributes, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UHeroAttributes, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UHeroAttributes, Mana, COND_None, REPNOTIFY_Always);
}

void UHeroAttributes::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UHeroAttributes, Health, OldHealth);
}

void UHeroAttributes::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UHeroAttributes, MaxHealth, OldMaxHealth);
}

void UHeroAttributes::OnRep_Mana(const FGameplayAttributeData& OldMana)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UHeroAttributes, Mana, OldMana);
}
