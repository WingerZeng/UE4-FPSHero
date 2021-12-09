// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FPSHeroPlayerStateBase.generated.h"

/**
 * 
 */
UCLASS()
class FPSHERO_API AFPSHeroPlayerStateBase : public APlayerState
{
	GENERATED_BODY()

public:
	AFPSHeroPlayerStateBase();

	UFUNCTION(BlueprintCallable)
	int GetMoney() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void SetMoney(int NewMoney);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, BlueprintImplementableEvent)
	void CharacterDie();
	
	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable)
	void SetTeam(int Team);

	UFUNCTION(BlueprintCallable)
	int GetTeam() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, BlueprintAuthorityOnly)
	void Birth();

	UFUNCTION(BlueprintCallable)
	APlayerController* 	GetPlayerController();
	
protected:
	UPROPERTY(Replicated)
	int Money = 0;
	
	UPROPERTY(Replicated)
	int TeamID;
};
