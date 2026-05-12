// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Logging/LogMacros.h"

// Includi il sistema debug professionale PRIMA del .generated.h.
// Regola UE5: .generated.h deve essere SEMPRE l'ultimo #include.
// Tutti i file che includono questo header ereditano automaticamente
// FDebugUtils e la categoria LogMenuSystemOnline.
#include "DebugUtils.h"

// DEVE essere l'ultimo include — regola obbligatoria di Unreal Header Tool (UHT)
#include "MenuSystemCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

// ============================================================
//  CATEGORIA LOG GENERALE DEL PERSONAGGIO
//  Separata da LogMenuSystemOnline (multiplayer) per filtrare
//  indipendentemente nel Output Log dell'editor.
// ============================================================

/**
 * @brief Categoria log per input, camera e inizializzazione del personaggio.
 *        I log multiplayer usano invece LogMenuSystemOnline (in DebugUtils.h).
 */
DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);


// ============================================================
//  AMenuSystemCharacter
// ============================================================

/**
 * @brief Personaggio principale controllabile in terza persona.
 *
 * Questa classe gestisce:
 *   - Movimento 3D con camera su SpringArm
 *   - Input tramite Enhanced Input System (WASD + mouse/gamepad)
 *   - Sistema completo di sessioni multiplayer Steam
 *
 * --- FLUSSO HOST ---
 * @code
 *   CreateGameSession()
 *     -> [sessione esistente?] DestroySession() -> OnDestroySessionComplete() -> CreateGameSession()
 *     -> CreateSession() ASYNC
 *     -> OnCreateSessionComplete()
 *     -> ServerTravel("/Game/.../Lobby?listen")
 * @endcode
 *
 * --- FLUSSO CLIENT ---
 * @code
 *   JoinGameSession()
 *     -> FindSessions() ASYNC
 *     -> OnFindSessionsComplete()
 *     -> JoinSession() ASYNC
 *     -> OnJoinSessionsComplete()
 *     -> ClientTravel(Address)
 * @endcode
 *
 * --- BUG CORRETTI ---
 *   #1: FindSessionsCompleteDelegateHandle non veniva salvato dopo la registrazione
 *   #2: JoinSessionCompleteDelegateHandle non veniva salvato dopo la registrazione
 *   #3: DestroySessionCompleteDelegate non inizializzato; OnDestroySessionComplete non implementato
 *   #4: FindSessionsCompleteDelegate non veniva pulito in OnFindSessionsComplete
 *
 * @note Classe ABSTRACT: deve essere subclassata da un Blueprint che assegna
 *       mesh, animazioni e Input Actions.
 */
UCLASS(abstract)
class AMenuSystemCharacter : public ACharacter
{
	GENERATED_BODY()

	// =======================================================
	// COMPONENTI CAMERA
	// =======================================================

	/**
	 * @brief Braccio che posiziona la camera dietro il personaggio.
	 *        Si accorcia automaticamente in caso di collisione con la geometria.
	 *        bUsePawnControlRotation = true: ruota con il controller del player.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/**
	 * @brief Camera principale che segue il personaggio.
	 *        Attaccata al socket del CameraBoom.
	 *        bUsePawnControlRotation = false: non ruota autonomamente.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;

protected:

	// =======================================================
	// INPUT ACTIONS (da assegnare nel Blueprint derivato)
	// =======================================================

	/**
	 * @brief Azione di salto (bindato a Jump / StopJumping).
	 *        Il Blueprint deve assegnarla dall'Input Mapping Context.
	 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/**
	 * @brief Azione di movimento WASD / stick sinistro.
	 *        Restituisce Vector2D: X = destra/sinistra, Y = avanti/indietro.
	 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/**
	 * @brief Azione di rotazione camera da gamepad (stick destro).
	 *        Restituisce Vector2D: X = yaw, Y = pitch.
	 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/**
	 * @brief Azione di rotazione camera da mouse.
	 *        Separata da LookAction per binding differenziati nello stesso IMC.
	 *        Usa lo stesso handler Look().
	 */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

public:

	// =======================================================
	// COSTRUTTORE
	// =======================================================

	/**
	 * @brief Inizializza componenti fisici, camera, movimento,
	 *        tutti e quattro i delegate online e il subsistema Steam.
	 *
	 * @note Se Steam non è avviato, OnlineSessionInterface risulterà invalida
	 *       e tutti i metodi di sessione eseguiranno un early return con log di errore.
	 */
	AMenuSystemCharacter();

protected:

	// =======================================================
	// OVERRIDE UNREAL
	// =======================================================

	/**
	 * @brief Registra i binding Enhanced Input al momento del possesso del pawn.
	 *
	 * @param PlayerInputComponent Componente a cui aggiungere i binding.
	 *
	 * @note Supporta SOLO Enhanced Input. Se il componente non è UEnhancedInputComponent
	 *       viene loggato un errore e non vengono registrati binding.
	 */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	// =======================================================
	// INPUT HANDLERS PRIVATI
	// Ricevono il valore raw dall'Enhanced Input e lo smistano
	// ai metodi pubblici Do*() separando input da logica.
	// =======================================================

	/**
	 * @brief Riceve il valore Vector2D dal MoveAction e chiama DoMove().
	 * @param Value Vector2D: X = destra/sinistra, Y = avanti/indietro.
	 */
	void Move(const FInputActionValue& Value);

	/**
	 * @brief Riceve il valore Vector2D da LookAction o MouseLookAction e chiama DoLook().
	 * @param Value Vector2D: X = yaw (orizzontale), Y = pitch (verticale).
	 */
	void Look(const FInputActionValue& Value);

public:

	// =======================================================
	// INPUT PUBBLICI
	// Esposti come BlueprintCallable per essere chiamabili
	// anche da widget UI on-screen (mobile/touch).
	// =======================================================

	/**
	 * @brief Applica il movimento al personaggio relativo alla direzione della camera.
	 *
	 * @param Right   Asse laterale: negativo = sinistra, positivo = destra.
	 * @param Forward Asse frontale: negativo = indietro, positivo = avanti.
	 *
	 * @note Se GetController() == nullptr non fa nulla (sicuro da chiamare sempre).
	 */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/**
	 * @brief Ruota la camera tramite il controller del personaggio.
	 *
	 * @param Yaw   Rotazione orizzontale (sinistra/destra).
	 * @param Pitch Rotazione verticale (su/giù).
	 *
	 * @note Se GetController() == nullptr non fa nulla.
	 */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/**
	 * @brief Avvia il salto chiamando ACharacter::Jump().
	 *        Compatibile con il sistema JumpMaxHoldTime del CMC.
	 */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/**
	 * @brief Termina il salto chiamando ACharacter::StopJumping().
	 *        Necessario per il press-and-hold jump.
	 */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	// =======================================================
	// GETTERS COMPONENTI
	// =======================================================

	/** @return Puntatore al CameraBoom (SpringArm). Valido dopo la costruzione. */
	FORCEINLINE class USpringArmComponent* GetCameraBoom()  const { return CameraBoom;   }

	/** @return Puntatore alla FollowCamera (CameraComponent). Valido dopo la costruzione. */
	FORCEINLINE class UCameraComponent*    GetFollowCamera() const { return FollowCamera; }


	// =======================================================
	// ONLINE SESSION SYSTEM
	// =======================================================

public:

	/**
	 * @brief Interfaccia al sottosistema online (Steam / EOS / NULL).
	 *
	 * Inizializzata nel costruttore tramite IOnlineSubsystem::Get()->GetSessionInterface().
	 * Espone tutte le operazioni di rete: create, find, join, destroy session.
	 *
	 * @note Invalida se:
	 *         - Steam non è avviato
	 *         - Plugin OnlineSubsystemSteam disabilitato nel .uproject
	 *         - SteamDevAppId mancante nel DefaultEngine.ini
	 *       Controlla sempre IsValid() prima di usarla.
	 */
	IOnlineSessionPtr OnlineSessionInterface;

protected:

	// =======================================================
	// FUNZIONI SESSIONE (triggerable da Blueprint o UI)
	// =======================================================

	/**
	 * @brief Crea una nuova sessione di gioco su Steam.
	 *
	 * Flusso:
	 *   1. Verifica OnlineSessionInterface valida
	 *   2. Se esiste già una sessione: la distrugge (bCreateSessionOnDestroy=true)
	 *      e aspetta OnDestroySessionComplete per ricrearla
	 *   3. Registra CreateSessionCompleteDelegate e salva l'handle
	 *   4. Configura FOnlineSessionSettings (LAN=false, 4 slot, Presence, Lobbies)
	 *   5. Imposta MatchType = "FreeForAll" come chiave cercabile
	 *   6. Chiama CreateSession() ASYNC -> callback OnCreateSessionComplete
	 *
	 * @note BUG FIX #3: il DestroySessionCompleteDelegate ora è correttamente
	 *       inizializzato nel costruttore e OnDestroySessionComplete è implementato.
	 */
	UFUNCTION(BlueprintCallable, Category="Online|Session")
	void CreateGameSession();

	/**
	 * @brief Cerca le sessioni disponibili su Steam e fa il join automatico.
	 *
	 * Flusso:
	 *   1. Verifica OnlineSessionInterface valida
	 *   2. Verifica che non sia già in corso una ricerca
	 *   3. Registra FindSessionsCompleteDelegate e SALVA l'handle (bug fix #1)
	 *   4. Crea FOnlineSessionSearch: MaxResults=10000, Presence=true, Lobbies=true
	 *   5. Verifica UniqueNetId del player locale
	 *   6. Chiama FindSessions() ASYNC -> callback OnFindSessionsComplete
	 *
	 * @note BUG FIX #1: FindSessionsCompleteDelegateHandle ora viene salvato.
	 */
	UFUNCTION(BlueprintCallable, Category="Online|Session")
	void JoinGameSession();

	// =======================================================
	// CALLBACKS ONLINE SUBSYSTEM
	// Chiamate automaticamente dall'OnlineSubsystem.
	// NON richiamare manualmente.
	// =======================================================

	/**
	 * @brief Chiamata quando CreateSession() completa.
	 *        Pulisce il delegate, poi esegue ServerTravel verso la Lobby se successo.
	 *
	 * @param SessionName    Nome della sessione (NAME_GameSession).
	 * @param bWasSuccessful true = sessione creata, false = errore.
	 */
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/**
	 * @brief Chiamata quando DestroySession() completa.
	 *        Se bCreateSessionOnDestroy==true, richiama CreateGameSession().
	 *
	 * @param SessionName    Nome della sessione distrutta.
	 * @param bWasSuccessful true = distruzione riuscita.
	 *
	 * @note BUG FIX #3: questa funzione era dichiarata ma MAI implementata nel .cpp
	 *       originale. Il flusso si bloccava dopo la distruzione.
	 */
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	/**
	 * @brief Chiamata quando FindSessions() completa.
	 *        Logga tutti i risultati (Id, Host, Ping, Slot, MatchType).
	 *        Per la prima sessione FreeForAll con slot liberi: avvia JoinSession().
	 *
	 * @param bWasSuccessful true = ricerca completata senza errori.
	 *
	 * @note BUG FIX #1/#4: handle ora salvato e delegate pulito a inizio callback.
	 */
	void OnFindSessionsComplete(bool bWasSuccessful);

	/**
	 * @brief Chiamata quando JoinSession() completa.
	 *        Se Success: GetResolvedConnectString() + ClientTravel().
	 *        Se errore: logga il tipo specifico (piena, non esiste, ecc.).
	 *        In ogni caso pulisce il JoinSessionCompleteDelegate.
	 *
	 * @param SessionName Nome della sessione.
	 * @param Result      Esito: Success | SessionIsFull | SessionDoesNotExist |
	 *                    CouldNotRetrieveAddress | AlreadyInSession | UnknownError.
	 *
	 * @note BUG FIX #2: JoinSessionCompleteDelegateHandle ora viene salvato.
	 */
	void OnJoinSessionsComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

private:

	// =======================================================
	// DELEGATES
	// Wrappano le callback. Tutti inizializzati nel costruttore.
	// =======================================================

	/** Delegate -> OnCreateSessionComplete. */
	FOnCreateSessionCompleteDelegate  CreateSessionCompleteDelegate;

	/**
	 * Delegate -> OnDestroySessionComplete.
	 * @note BUG FIX #3: ora è inizializzato nel costruttore.
	 */
	FOnDestroySessionCompleteDelegate DestroySessionCompleteDelegate;

	/** Delegate -> OnFindSessionsComplete. */
	FOnFindSessionsCompleteDelegate   FindSessionsCompleteDelegate;

	/** Delegate -> OnJoinSessionsComplete. */
	FOnJoinSessionCompleteDelegate    JoinSessionCompleteDelegate;

	// =======================================================
	// DELEGATE HANDLES
	// Conservati per poter rimuovere i delegate con Clear*_Handle().
	// Senza Clear il delegate rimane attivo causando doppi callback.
	// BUG FIX #1 e #2: Find e Join handle ora vengono salvati.
	// =======================================================

	FDelegateHandle CreateSessionCompleteDelegateHandle;
	FDelegateHandle DestroySessionCompleteDelegateHandle;

	/** @note BUG FIX #1: prima non veniva mai salvato. */
	FDelegateHandle FindSessionsCompleteDelegateHandle;

	/** @note BUG FIX #2: prima non veniva mai salvato. */
	FDelegateHandle JoinSessionCompleteDelegateHandle;

	// =======================================================
	// STATO SESSIONE
	// =======================================================

	/** Risultati e parametri di FindSessions(). Popolato in JoinGameSession(). */
	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	/**
	 * @brief true = dopo la distruzione della sessione corrente, ricrearla.
	 *        Impostato in CreateGameSession() quando esiste già una sessione.
	 *        Resettato in OnDestroySessionComplete() dopo aver richiamato CreateGameSession().
	 */
	bool bCreateSessionOnDestroy = false;
};