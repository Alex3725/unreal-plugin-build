// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Logging/LogMacros.h"
#include "MenuSystemCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  Personaggio principale controllabile in terza persona.
 *  Gestisce camera, movimento e l'intero sistema di sessioni online (Steam/EOS/NULL).
 */
UCLASS(abstract)
class AMenuSystemCharacter : public ACharacter
{
	GENERATED_BODY()

	// -------------------------------------------------------
	// COMPONENTI CAMERA
	// -------------------------------------------------------

	/** Braccio che posiziona la camera dietro il personaggio.
	 *  Si accorcia automaticamente in caso di collisione con la geometria. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Camera che segue il personaggio. Non ruota da sola: segue il CameraBoom. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

protected:

	// -------------------------------------------------------
	// INPUT ACTIONS (assegnate nel Blueprint)
	// -------------------------------------------------------

	/** Azione di salto */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Azione di movimento (WASD / stick sinistro) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Azione di rotazione camera (gamepad) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Azione di rotazione camera (mouse) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	/** Costruttore: inizializza componenti, delegates e subsistema online */
	AMenuSystemCharacter();

protected:

	/** Registra tutti i binding di input con l'Enhanced Input System */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	// -------------------------------------------------------
	// INPUT HANDLERS (privati, chiamati dai binding)
	// -------------------------------------------------------

	/** Riceve l'input di movimento e lo passa a DoMove() */
	void Move(const FInputActionValue& Value);

	/** Riceve l'input di rotazione e lo passa a DoLook() */
	void Look(const FInputActionValue& Value);

public:

	// -------------------------------------------------------
	// INPUT PUBBLICI (chiamabili anche da Blueprint o UI)
	// -------------------------------------------------------

	/** Muove il personaggio. Right = asse X, Forward = asse Y */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Ruota la camera. Yaw = orizzontale, Pitch = verticale */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Avvia il salto */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Termina il salto (rilascio tasto) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	// -------------------------------------------------------
	// GETTERS COMPONENTI
	// -------------------------------------------------------

	FORCEINLINE class USpringArmComponent* GetCameraBoom()  const { return CameraBoom;   }
	FORCEINLINE class UCameraComponent*    GetFollowCamera() const { return FollowCamera; }

	// =======================================================
	// ONLINE SESSION SYSTEM
	// =======================================================

public:

	/**
	 *  Interfaccia al sottosistema online (Steam, EOS, NULL ecc.).
	 *  Viene inizializzata nel costruttore tramite IOnlineSubsystem::Get().
	 *  Usata per creare, cercare e joinare sessioni multiplayer.
	 */
	IOnlineSessionPtr OnlineSessionInterface;

protected:

	// -------------------------------------------------------
	// FUNZIONI PUBBLICHE SESSIONE (chiamabili da Blueprint)
	// -------------------------------------------------------

	/**
	 *  Crea una nuova sessione di gioco su Steam.
	 *  Se ne esiste già una attiva, la distrugge prima (tramite OnDestroySessionComplete)
	 *  e poi la ricrea automaticamente.
	 */
	UFUNCTION(BlueprintCallable, Category="Online")
	void CreateGameSession();

	/**
	 *  Avvia la ricerca delle sessioni disponibili su Steam.
	 *  Quando la ricerca termina viene chiamato OnFindSessionsComplete.
	 *  Se trova una sessione con MatchType == "FreeForAll" esegue il join automatico.
	 */
	UFUNCTION(BlueprintCallable, Category="Online")
	void JoinGameSession();

	// -------------------------------------------------------
	// CALLBACKS (chiamate automaticamente dall'Online Subsystem)
	// -------------------------------------------------------

	/**
	 *  Chiamata quando CreateSession() termina.
	 *  Pulisce il delegate PRIMA di fare ServerTravel verso la Lobby.
	 *
	 *  @param SessionName    Nome della sessione creata
	 *  @param bWasSuccessful True se la creazione è riuscita
	 */
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/**
	 *  Chiamata quando DestroySession() termina.
	 *  Se ha successo richiama CreateGameSession() per ricreare la sessione.
	 *  Risolve il bug in cui una seconda chiamata a CreateGameSession
	 *  distrugge la sessione ma non la ricrea mai.
	 *
	 *  @param SessionName    Nome della sessione distrutta
	 *  @param bWasSuccessful True se la distruzione è riuscita
	 */
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	/**
	 *  Chiamata quando FindSessions() termina.
	 *  Itera sui risultati e tenta il join della prima sessione
	 *  con MatchType == "FreeForAll". Pulisce il delegate FindSessions.
	 *
	 *  @param bWasSuccessful True se la ricerca è riuscita
	 */
	void OnFindSessionsComplete(bool bWasSuccessful);

	/**
	 *  Chiamata quando JoinSession() termina.
	 *  Risolve l'indirizzo del server e chiama ClientTravel.
	 *  Pulisce il delegate JoinSession.
	 *
	 *  @param SessionName  Nome della sessione joinata
	 *  @param Result       Esito del join (Success, AlreadyInSession, ecc.)
	 */
	void OnJoinSessionsComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

private:

	// -------------------------------------------------------
	// DELEGATES
	// Wrappano le callback e vengono passati all'Online Subsystem
	// che li chiama al momento opportuno (es. quando FindSessions finisce).
	// -------------------------------------------------------

	/** Delegate per la creazione sessione */
	FOnCreateSessionCompleteDelegate  CreateSessionCompleteDelegate;

	/** Delegate per la distruzione sessione (usato prima di ricreare) */
	FOnDestroySessionCompleteDelegate DestroySessionCompleteDelegate;

	/** Delegate per la ricerca sessioni */
	FOnFindSessionsCompleteDelegate   FindSessionsCompleteDelegate;

	/** Delegate per il join sessione */
	FOnJoinSessionCompleteDelegate    JoinSessionCompleteDelegate;

	// -------------------------------------------------------
	// DELEGATE HANDLES
	// Quando registri un delegate ottieni un Handle.
	// Serve per poterlo rimuovere (Clear) dopo l'uso:
	// senza Clear il delegate rimane appeso e causa doppi callback o crash.
	// -------------------------------------------------------

	FDelegateHandle CreateSessionCompleteDelegateHandle;
	FDelegateHandle DestroySessionCompleteDelegateHandle;
	FDelegateHandle FindSessionsCompleteDelegateHandle;
	FDelegateHandle JoinSessionCompleteDelegateHandle;

	// -------------------------------------------------------
	// RICERCA SESSIONI
	// -------------------------------------------------------

	/** Contiene parametri di ricerca e risultati di FindSessions() */
	TSharedPtr<FOnlineSessionSearch> SessionSearch;
};