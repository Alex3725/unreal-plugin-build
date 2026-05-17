// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Logging/LogMacros.h"

// ============================================================
// FIX COMPILAZIONE #9:
// OnlineSessionSettings.h va incluso ESPLICITAMENTE nel header.
// In UE 5.7, Interfaces/OnlineSessionInterface.h NON include
// transitivamente FOnlineSessionSearchResult, causando:
//   error C2079: 'PendingInviteResult' utilizza class non definito
//   error C2664: impossibile convertire '<error type>'
// ============================================================
#include "OnlineSessionSettings.h"

// Includi il sistema debug professionale PRIMA del .generated.h.
// Regola UE5: .generated.h deve essere SEMPRE l'ultimo #include.
#include "DebugUtils.h"

// DEVE essere l'ultimo include — regola obbligatoria di UHT
#include "MenuSystemCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

// ============================================================
//  CATEGORIA LOG GENERALE DEL PERSONAGGIO
// ============================================================
DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);


// ============================================================
//  AMenuSystemCharacter
// ============================================================

/**
 * @brief Personaggio principale controllabile in terza persona.
 *
 * --- FLUSSO HOST ---
 * @code
 *   CreateGameSession()
 *     -> [sessione esistente?] DestroySession()
 *        -> OnDestroySessionComplete() -> CreateGameSession()
 *     -> CreateSession() ASYNC
 *     -> OnCreateSessionComplete()
 *     -> [DELAY 0.5s — FIX #10] OpenLevel("Lobby", listen)
 * @endcode
 *
 * --- FLUSSO CLIENT ---
 * @code
 *   JoinGameSession()
 *     -> FindSessions() ASYNC
 *     -> OnFindSessionsComplete()
 *     -> InternalJoinSession()
 *     -> OnJoinSessionsComplete()
 *     -> ClientTravel(Address)
 * @endcode
 *
 * --- FLUSSO INVITO STEAM ---
 * @code
 *   OnSessionUserInviteAccepted()
 *     -> [sessione esistente?] DestroySession()
 *        -> OnDestroySessionComplete() -> InternalJoinSession()
 *     -> [no sessione] InternalJoinSession() direttamente
 *     -> OnJoinSessionsComplete() -> ClientTravel(Address)
 * @endcode
 *
 * --- BUG CORRETTI (storico) ---
 *   #1:  FindSessionsCompleteDelegateHandle non salvato
 *   #2:  JoinSessionCompleteDelegateHandle non salvato
 *   #3:  DestroySessionCompleteDelegate non init + non implementato
 *   #4:  FindSessionsCompleteDelegate non pulito in OnFindSessionsComplete
 *   #5:  ServerTravel -> OpenLevel per listen server
 *   #6:  InviteAcceptedDelegate registrato
 *   #7:  SessionUserInviteAcceptedDelegateHandle pulito in EndPlay
 *   #8:  OnSessionUserInviteAccepted gestisce caso "già in sessione"
 *   #9:  COMPILAZIONE — #include "OnlineSessionSettings.h" aggiunto
 *   #10: DELAY 0.5s prima di OpenLevel (Steam lobby registration)
 *   #11: Rimosso SEARCH_PRESENCE (conflitto UE5.3+ con SEARCH_LOBBIES)
 *   #12: GetPreferredUniqueNetId() in CreateSession (API moderna)
 *   #13: MaxSearchResults = 100 (Steam limita internamente a ~500)
 *   #14: SessionSearch.Reset() dopo OnFindSessionsComplete
 *   #15: DEFINE_LOG_CATEGORY(LogTemplateCharacter) nel .cpp
 */
UCLASS(abstract)
class AMenuSystemCharacter : public ACharacter
{
	GENERATED_BODY()

	// =======================================================
	// COMPONENTI CAMERA
	// =======================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

protected:

	// =======================================================
	// INPUT ACTIONS
	// =======================================================

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	AMenuSystemCharacter();

protected:

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/**
	 * @brief Pulizia delegate e timer prima della distruzione dell'attore.
	 *
	 * Pulisce:
	 *   - SessionUserInviteAcceptedDelegateHandle (fix #7: evita handle dangling)
	 *   - LobbyTravelTimerHandle (fix #10: evita callback lambda su attore distrutto)
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

public:

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	FORCEINLINE class USpringArmComponent* GetCameraBoom()  const { return CameraBoom;   }
	FORCEINLINE class UCameraComponent*    GetFollowCamera() const { return FollowCamera; }

	// =======================================================
	// ONLINE SESSION SYSTEM
	// =======================================================

public:

	/**
	 * @brief Interfaccia al sottosistema online (Steam).
	 * Invalida se Steam non è avviato o il plugin è disabilitato.
	 */
	IOnlineSessionPtr OnlineSessionInterface;

protected:

	/**
	 * @brief Crea una nuova sessione su Steam.
	 * FIX #12: usa GetPreferredUniqueNetId() invece di GetControllerId().
	 */
	UFUNCTION(BlueprintCallable, Category="Online|Session")
	void CreateGameSession();

	/**
	 * @brief Cerca e unisce automaticamente una sessione FreeForAll.
	 * FIX #11: solo SEARCH_LOBBIES (rimosso SEARCH_PRESENCE).
	 * FIX #13: MaxSearchResults = 100.
	 * FIX #14: SessionSearch.Reset() alla fine della callback.
	 */
	UFUNCTION(BlueprintCallable, Category="Online|Session")
	void JoinGameSession();

	// =======================================================
	// CALLBACKS ONLINE SUBSYSTEM
	// =======================================================

	/**
	 * @brief Callback CreateSession.
	 * FIX #10: ritarda OpenLevel di 0.5s per dare tempo a Steam
	 *          di registrare la lobby prima che il server ascolti.
	 */
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/**
	 * @brief Callback DestroySession.
	 * Gestisce tre casi:
	 *   bCreateSessionOnDestroy -> ricrea sessione
	 *   bJoinSessionOnDestroy   -> join con PendingInviteResult
	 *   altrimenti              -> nessuna azione
	 */
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	/** @brief Callback FindSessions. */
	void OnFindSessionsComplete(bool bWasSuccessful);

	/** @brief Callback JoinSession. ClientTravel se successo. */
	void OnJoinSessionsComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	/**
	 * @brief Callback invito Steam (overlay Shift+Tab).
	 * FIX #8: se già in sessione, la distrugge prima del join.
	 */
	void OnSessionUserInviteAccepted(
		bool bWasSuccessful,
		int32 ControllerId,
		TSharedPtr<const FUniqueNetId> UserId,
		const FOnlineSessionSearchResult& InviteResult
	);

	/**
	 * @brief Esegue JoinSession su un risultato specifico.
	 * Usato da OnFindSessionsComplete e da OnDestroySessionComplete
	 * per evitare duplicazione del codice di join.
	 */
	void InternalJoinSession(const FOnlineSessionSearchResult& SessionResult);

private:

	// =======================================================
	// DELEGATES
	// =======================================================

	FOnCreateSessionCompleteDelegate     CreateSessionCompleteDelegate;
	FOnDestroySessionCompleteDelegate    DestroySessionCompleteDelegate;
	FOnFindSessionsCompleteDelegate      FindSessionsCompleteDelegate;
	FOnJoinSessionCompleteDelegate       JoinSessionCompleteDelegate;
	FOnSessionUserInviteAcceptedDelegate SessionUserInviteAcceptedDelegate;

	// =======================================================
	// DELEGATE HANDLES
	// =======================================================

	FDelegateHandle CreateSessionCompleteDelegateHandle;
	FDelegateHandle DestroySessionCompleteDelegateHandle;
	FDelegateHandle FindSessionsCompleteDelegateHandle;
	FDelegateHandle JoinSessionCompleteDelegateHandle;

	/**
	 * FIX #7: handle per l'invite delegate.
	 * Viene rimosso esplicitamente in EndPlay() per evitare
	 * handle dangling al cambio di livello.
	 */
	FDelegateHandle SessionUserInviteAcceptedDelegateHandle;

	// =======================================================
	// STATO SESSIONE
	// =======================================================

	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	/** true = dopo destroy, ricrea la sessione. */
	bool bCreateSessionOnDestroy = false;

	/**
	 * true = dopo destroy, esegui InternalJoinSession(PendingInviteResult).
	 * FIX #8: flusso invito Steam con sessione già attiva.
	 */
	bool bJoinSessionOnDestroy = false;

	/**
	 * Dati dell'invito Steam in attesa.
	 * Valido solo quando bJoinSessionOnDestroy == true.
	 *
	 * FIX #9: richiede #include "OnlineSessionSettings.h" (aggiunto sopra).
	 */
	FOnlineSessionSearchResult PendingInviteResult;

	/**
	 * Handle del timer che ritarda l'OpenLevel di 0.5s.
	 * FIX #10: Steam registra la lobby in modo asincrono.
	 *          Aprire il listen server troppo presto rende la sessione
	 *          invisibile e il join impossibile.
	 *          Pulito in EndPlay() per sicurezza.
	 */
	FTimerHandle LobbyTravelTimerHandle;
};