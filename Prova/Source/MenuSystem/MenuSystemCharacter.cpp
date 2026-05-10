// Copyright Epic Games, Inc. All Rights Reserved.

#include "MenuSystemCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "MenuSystem.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Online/OnlineSessionNames.h"

// DebugUtils.h già incluso tramite MenuSystemCharacter.h
// (fornisce FDebugUtils e LogMenuSystemOnline)

// ============================================================
//  DEFINIZIONE CATEGORIA LOG (dichiarata nell'header)
//  Va definita UNA SOLA VOLTA in tutto il progetto.
// ============================================================
DEFINE_LOG_CATEGORY(LogTemplateCharacter);


// ============================================================
//  COSTRUTTORE
// ============================================================

AMenuSystemCharacter::AMenuSystemCharacter():

	// ---------------------------------------------------------
	// Inizializzazione di TUTTI e quattro i delegate online.
	//
	// IMPORTANTE (BUG FIX #3):
	// Il DestroySessionCompleteDelegate NON era presente qui
	// nella versione originale. Questo causava il blocco del
	// flusso: dopo DestroySession() la callback non scattava
	// mai e la sessione non veniva mai ricreata.
	// ---------------------------------------------------------

	// Delegate creazione: quando CreateSession() finisce -> OnCreateSessionComplete
	CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(
		this, &AMenuSystemCharacter::OnCreateSessionComplete)),

	// Delegate distruzione: quando DestroySession() finisce -> OnDestroySessionComplete
	// BUG FIX #3: aggiunto. Prima mancava completamente.
	DestroySessionCompleteDelegate(FOnDestroySessionCompleteDelegate::CreateUObject(
		this, &AMenuSystemCharacter::OnDestroySessionComplete)),

	// Delegate ricerca: quando FindSessions() finisce -> OnFindSessionsComplete
	FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(
		this, &ThisClass::OnFindSessionsComplete)),

	// Delegate join: quando JoinSession() finisce -> OnJoinSessionsComplete
	JoinSessionCompleteDelegate(FOnJoinSessionCompleteDelegate::CreateUObject(
		this, &ThisClass::OnJoinSessionsComplete))

{
	// -------------------------------------------------------
	// CONFIGURAZIONE CAPSULA DI COLLISIONE
	// -------------------------------------------------------
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// -------------------------------------------------------
	// ROTAZIONE: il personaggio NON ruota con il controller.
	// La camera ruota, il personaggio segue il movimento.
	// -------------------------------------------------------
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	// -------------------------------------------------------
	// CONFIGURAZIONE MOVIMENTO
	// bOrientRotationToMovement: il personaggio guarda nella
	// direzione in cui si muove (non verso la camera).
	// -------------------------------------------------------
	GetCharacterMovement()->bOrientRotationToMovement    = true;
	GetCharacterMovement()->RotationRate                 = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->JumpZVelocity                = 500.f;
	GetCharacterMovement()->AirControl                   = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed                 = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed           = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking   = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling   = 1500.0f;

	// -------------------------------------------------------
	// CAMERA BOOM (SpringArm)
	// TargetArmLength: distanza camera-personaggio a riposo.
	// bUsePawnControlRotation: la camera ruota con il controller.
	// -------------------------------------------------------
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength       = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// -------------------------------------------------------
	// FOLLOW CAMERA
	// Attaccata al socket del CameraBoom.
	// bUsePawnControlRotation=false: non ruota da sola,
	// eredita la rotazione dal CameraBoom.
	// -------------------------------------------------------
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// -------------------------------------------------------
	// RESET FLAG sessione
	// -------------------------------------------------------
	bCreateSessionOnDestroy = false;

	// =======================================================
	//  INIZIALIZZAZIONE ONLINE SUBSYSTEM
	// =======================================================

	FDebugUtils::Section(TEXT("ONLINE SUBSYSTEM INIT"));

	// Recupera il subsistema online configurato nel DefaultEngine.ini
	// (di solito Steam, ma può essere NULL in editor o EOS in produzione)
	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();

	if (!Subsystem)
	{
		// Nessun subsistema trovato: Steam non è avviato o il plugin è disabilitato
		FDebugUtils::Error(TEXT("[INIT] ERRORE CRITICO: nessun subsistema online trovato!"));
		FDebugUtils::Warning(TEXT("[INIT] Controlla: 1) Steam avviato? 2) Plugin OnlineSubsystemSteam abilitato nel .uproject?"));
		return;
	}

	// Subsistema trovato: logga il nome (es. "Steam", "NULL", "EOS")
	FDebugUtils::Success(FString::Printf(
		TEXT("[INIT] Subsistema online trovato: '%s'"),
		*Subsystem->GetSubsystemName().ToString()
	));

	// Recupera l'interfaccia per la gestione delle sessioni
	OnlineSessionInterface = Subsystem->GetSessionInterface();

	if (OnlineSessionInterface.IsValid())
	{
		FDebugUtils::Success(TEXT("[INIT] OnlineSessionInterface valida e pronta."));
	}
	else
	{
		// L'interfaccia non è disponibile: non potremo creare/cercare sessioni
		FDebugUtils::Error(TEXT("[INIT] ERRORE: OnlineSessionInterface NON valida!"));
		FDebugUtils::Warning(TEXT("[INIT] Le funzioni di sessione non funzioneranno."));
	}

	FDebugUtils::Separator();
}


// ============================================================
//  INPUT SETUP
// ============================================================

/**
 * Registra i binding Enhanced Input.
 * Chiamato automaticamente da Unreal durante il possesso del pawn.
 */
void AMenuSystemCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Salto: avvio e rilascio
		EnhancedInputComponent->BindAction(JumpAction,      ETriggerEvent::Started,   this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction,      ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Movimento (WASD / stick sinistro)
		EnhancedInputComponent->BindAction(MoveAction,      ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Move);

		// Rotazione camera: mouse
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Look);

		// Rotazione camera: gamepad
		EnhancedInputComponent->BindAction(LookAction,      ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Look);
	}
	else
	{
		// Errore grave: senza Enhanced Input il personaggio non risponde ai comandi
		UE_LOG(LogTemplateCharacter, Error,
			TEXT("'%s' non ha trovato un UEnhancedInputComponent! "
			     "Questo template richiede Enhanced Input. "
			     "Se usi il sistema legacy devi aggiornare il codice."),
			*GetNameSafe(this));
	}
}


// ============================================================
//  CREATE GAME SESSION
// ============================================================

void AMenuSystemCharacter::CreateGameSession()
{
	FDebugUtils::Section(TEXT("CREATE GAME SESSION"));

	// --- Guard: interfaccia online valida? ---
	if (!OnlineSessionInterface.IsValid())
	{
		FDebugUtils::Error(TEXT("[CREATE] OnlineSessionInterface non valida. Impossibile creare la sessione."));
		FDebugUtils::Warning(TEXT("[CREATE] Controlla: Steam avviato? Plugin Steam abilitato? SteamDevAppId in DefaultEngine.ini?"));
		return;
	}

	// -------------------------------------------------------
	// Controlla se esiste già una sessione attiva.
	// Se sì, la dobbiamo distruggere prima di crearne una nuova.
	// -------------------------------------------------------
	auto ExistingSession = OnlineSessionInterface->GetNamedSession(NAME_GameSession);

	if (ExistingSession != nullptr)
	{
		FDebugUtils::Warning(TEXT("[CREATE] Sessione esistente rilevata. La distruggo prima di crearne una nuova..."));
		FDebugUtils::Info(FString::Printf(
			TEXT("[CREATE] Sessione trovata: '%s' | Stato: %d"),
			*ExistingSession->SessionName.ToString(),
			(int32)ExistingSession->SessionState
		));

		// Imposta il flag: dopo la distruzione, ricreare la sessione
		bCreateSessionOnDestroy = true;

		// --- BUG FIX #3 ---
		// Registra il DestroyDelegate e SALVA l'handle.
		// Prima questo blocco non esisteva: DestroySession veniva chiamata
		// ma nessuna callback scattava per ricreare la sessione.
		DestroySessionCompleteDelegateHandle =
			OnlineSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegate);

		FDebugUtils::Info(TEXT("[CREATE] DestroyDelegate registrato. Avvio DestroySession() ASYNC..."));
		OnlineSessionInterface->DestroySession(NAME_GameSession);

		// Esci: il flusso continua in OnDestroySessionComplete()
		return;
	}

	FDebugUtils::Info(TEXT("[CREATE] Nessuna sessione esistente. Procedo con la creazione."));

	// -------------------------------------------------------
	// Registra il CreateDelegate e salva l'handle
	// -------------------------------------------------------
	CreateSessionCompleteDelegateHandle =
		OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);

	FDebugUtils::Info(TEXT("[CREATE] CreateDelegate registrato."));

	// -------------------------------------------------------
	// Configura le impostazioni della sessione
	// -------------------------------------------------------
	TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShared<FOnlineSessionSettings>();

	// false = sessione online su Steam (non LAN locale)
	SessionSettings->bIsLANMatch = false;

	// Numero massimo di giocatori connessi contemporaneamente
	SessionSettings->NumPublicConnections = 4;

	// true = la sessione appare nei risultati di ricerca degli altri giocatori
	SessionSettings->bShouldAdvertise = true;

	// true = richiesto da Steam per le sessioni basate su Lobby (Presence API)
	SessionSettings->bUsesPresence = true;

	// true = altri giocatori possono entrare anche dopo l'inizio della partita
	SessionSettings->bAllowJoinInProgress = true;

	// true = si può joinare tramite la presenza Steam (es. dal profilo amico)
	SessionSettings->bAllowJoinViaPresence = true;

	// true = usa il sistema Lobby di Steam se disponibile (più affidabile del P2P raw)
	SessionSettings->bUseLobbiesIfAvailable = true;

	// true = l'host può inviare inviti diretti agli amici Steam
	SessionSettings->bAllowInvites = true;

	// false = tutti possono cercare e joinare, non solo gli amici
	SessionSettings->bAllowJoinViaPresenceFriendsOnly = false;

	// Imposta una chiave custom "MatchType" = "FreeForAll"
	// I client cercheranno questa chiave in OnFindSessionsComplete
	// per identificare le sessioni di questo gioco specifico.
	// ViaOnlineServiceAndPing = visibile sia nel servizio Steam che nel ping diretto.
	SessionSettings->Set(
		FName("MatchType"),
		FString("FreeForAll"),
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing
	);

	FDebugUtils::Info(FString::Printf(
		TEXT("[CREATE] Settings: LAN=%s | Slot=%d | Advertise=%s | Presence=%s | Lobbies=%s"),
		SessionSettings->bIsLANMatch ? TEXT("SI") : TEXT("NO"),
		SessionSettings->NumPublicConnections,
		SessionSettings->bShouldAdvertise ? TEXT("SI") : TEXT("NO"),
		SessionSettings->bUsesPresence ? TEXT("SI") : TEXT("NO"),
		SessionSettings->bUseLobbiesIfAvailable ? TEXT("SI") : TEXT("NO")
	));

	// -------------------------------------------------------
	// Recupera il LocalPlayer per ottenere il ControllerId
	// -------------------------------------------------------
	ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

	if (!LocalPlayer)
	{
		FDebugUtils::Error(TEXT("[CREATE] ERRORE: GetFirstLocalPlayerFromController() ha restituito nullptr!"));
		// Pulisci il delegate visto che non procederemo
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		return;
	}

	FDebugUtils::Info(FString::Printf(
		TEXT("[CREATE] LocalPlayer trovato. ControllerId=%d. Avvio CreateSession() ASYNC..."),
		LocalPlayer->GetControllerId()
	));

	// -------------------------------------------------------
	// Avvia la creazione della sessione (operazione ASINCRONA)
	// Il risultato arriverà in OnCreateSessionComplete()
	// -------------------------------------------------------
	const bool bStarted = OnlineSessionInterface->CreateSession(
		LocalPlayer->GetControllerId(),
		NAME_GameSession,
		*SessionSettings
	);

	if (!bStarted)
	{
		// CreateSession() ha fallito immediatamente (errore sincrono)
		FDebugUtils::Error(TEXT("[CREATE] ERRORE: CreateSession() ha restituito false immediatamente!"));
		FDebugUtils::Warning(TEXT("[CREATE] Cause possibili: Steam non attivo, sessione già in creazione, SteamDevAppId errato."));

		// Pulisci il delegate dato che non ci sarà alcuna callback
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	}
	else
	{
		FDebugUtils::Success(TEXT("[CREATE] CreateSession() ASYNC avviato. Attendo callback..."));
	}
}


// ============================================================
//  CALLBACK: DESTROY SESSION COMPLETE
//  BUG FIX #3: questa funzione era DICHIARATA ma MAI IMPLEMENTATA
//  nella versione originale. Ora è correttamente implementata.
// ============================================================

void AMenuSystemCharacter::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	FDebugUtils::Section(TEXT("DESTROY SESSION COMPLETE"));

	FDebugUtils::Log(
		FString::Printf(TEXT("[DESTROY] Sessione '%s' | Risultato: %s"),
			*SessionName.ToString(),
			bWasSuccessful ? TEXT("SUCCESSO") : TEXT("FALLITO")
		),
		bWasSuccessful ? FColor::Green : FColor::Red
	);

	// Pulisci sempre il delegate, indipendentemente dall'esito
	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		FDebugUtils::Info(TEXT("[DESTROY] DestroyDelegate pulito."));
	}

	if (!bWasSuccessful)
	{
		// La distruzione è fallita: non possiamo procedere con la ricreazione
		FDebugUtils::Error(TEXT("[DESTROY] La distruzione della sessione è fallita. Impossibile procedere."));
		bCreateSessionOnDestroy = false;
		return;
	}

	// -------------------------------------------------------
	// Distruzione riuscita. Devo ricreare la sessione?
	// -------------------------------------------------------
	if (bCreateSessionOnDestroy)
	{
		FDebugUtils::Success(TEXT("[DESTROY] Sessione distrutta con successo. Riciclo: richiamo CreateGameSession()..."));
		bCreateSessionOnDestroy = false;
		CreateGameSession();
	}
	else
	{
		FDebugUtils::Info(TEXT("[DESTROY] Sessione distrutta. Nessuna ricreazione richiesta."));
	}

	FDebugUtils::Separator();
}


// ============================================================
//  CALLBACK: CREATE SESSION COMPLETE
// ============================================================

void AMenuSystemCharacter::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	FDebugUtils::Section(TEXT("CREATE SESSION COMPLETE"));

	FDebugUtils::Log(
		FString::Printf(TEXT("[CREATE CB] Sessione '%s' | Risultato: %s"),
			*SessionName.ToString(),
			bWasSuccessful ? TEXT("SUCCESSO") : TEXT("FALLITO")
		),
		bWasSuccessful ? FColor::Green : FColor::Red
	);

	// Pulisci il delegate appena possibile (buona pratica: farlo prima di qualsiasi return)
	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		FDebugUtils::Info(TEXT("[CREATE CB] CreateDelegate pulito."));
	}

	if (!bWasSuccessful)
	{
		FDebugUtils::Error(TEXT("[CREATE CB] La sessione non è stata creata."));
		FDebugUtils::Warning(TEXT("[CREATE CB] Controlla: 1) Steam avviato? 2) SteamDevAppId=480 in DefaultEngine.ini? 3) bEnabled=true per OnlineSubsystemSteam?"));
		return;
	}

	// -------------------------------------------------------
	// Sessione creata con successo.
	// Esegui ServerTravel verso la mappa Lobby in modalità Listen Server.
	// ?listen = questo server accetta anche giocatori remoti.
	// -------------------------------------------------------
	UWorld* World = GetWorld();

	if (!World)
	{
		FDebugUtils::Error(TEXT("[CREATE CB] ERRORE: GetWorld() ha restituito nullptr! ServerTravel non eseguito."));
		return;
	}

	FDebugUtils::Success(TEXT("[CREATE CB] Sessione creata! Eseguo ServerTravel verso /Game/ThirdPerson/Maps/Lobby?listen ..."));

	// Percorso: /Game/ corrisponde a Content/ nel progetto
	// ?listen = apre la mappa come Listen Server (l'host gioca sulla stessa istanza)
	World->ServerTravel(FString("/Game/ThirdPerson/Maps/Lobby?listen"));

	FDebugUtils::Separator();
}


// ============================================================
//  JOIN GAME SESSION
// ============================================================

void AMenuSystemCharacter::JoinGameSession()
{
	FDebugUtils::Section(TEXT("JOIN GAME SESSION - FIND"));

	// --- Guard: interfaccia online valida? ---
	if (!OnlineSessionInterface.IsValid())
	{
		FDebugUtils::Error(TEXT("[JOIN] OnlineSessionInterface non valida. Impossibile cercare sessioni."));
		FDebugUtils::Warning(TEXT("[JOIN] Controlla: Steam avviato? Plugin Steam abilitato nel .uproject?"));
		return;
	}

	// -------------------------------------------------------
	// Guard: evita ricerche multiple in parallelo
	// -------------------------------------------------------
	if (SessionSearch.IsValid() && SessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		FDebugUtils::Warning(TEXT("[JOIN] Una ricerca è già in corso. Ignoro la nuova richiesta."));
		return;
	}

	// -------------------------------------------------------
	// BUG FIX #1: registra il FindDelegate e SALVA l'handle.
	// Prima il risultato non veniva salvato, rendendo impossibile
	// rimuovere il delegate in OnFindSessionsComplete().
	// -------------------------------------------------------
	FindSessionsCompleteDelegateHandle =
		OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);

	FDebugUtils::Info(TEXT("[JOIN] FindSessionsDelegate registrato (handle salvato - BUG FIX #1)."));

	// -------------------------------------------------------
	// Crea l'oggetto di ricerca sessioni
	// -------------------------------------------------------
	SessionSearch = MakeShareable(new FOnlineSessionSearch());

	// Valore alto perché con SteamDevAppId=480 (default di test)
	// ci possono essere molte sessioni pubbliche di altri sviluppatori
	SessionSearch->MaxSearchResults = 10000;

	// false = ricerca online su Steam (non LAN)
	SessionSearch->bIsLanQuery = false;

	// -------------------------------------------------------
	// Filtro PRESENCE
	// Cerca solo sessioni con bUsesPresence=true.
	// NOTA: usiamo FName(TEXT("SEARCH_PRESENCE")) invece della
	// macro SEARCH_PRESENCE perché su alcune versioni di UE5
	// la macro non viene risolta correttamente a causa di un
	// problema di inclusione degli header OnlineSessionNames.
	// Il valore stringa è identico alla macro.
	// -------------------------------------------------------
	SessionSearch->QuerySettings.Set(
		FName(TEXT("SEARCH_PRESENCE")),
		true,
		EOnlineComparisonOp::Equals
	);

	// -------------------------------------------------------
	// Filtro LOBBIES
	// Cerca solo sessioni che usano il sistema Lobby di Steam,
	// coerente con bUseLobbiesIfAvailable=true in CreateGameSession().
	// SEARCH_LOBBIES è definito in Online/OnlineSessionNames.h.
	// -------------------------------------------------------
	SessionSearch->QuerySettings.Set(
		SEARCH_LOBBIES,
		true,
		EOnlineComparisonOp::Equals
	);

	FDebugUtils::Info(FString::Printf(
		TEXT("[JOIN] Parametri ricerca: MaxResults=%d | LAN=%s | Presence=true | Lobbies=true"),
		SessionSearch->MaxSearchResults,
		SessionSearch->bIsLanQuery ? TEXT("SI") : TEXT("NO")
	));

	// -------------------------------------------------------
	// Recupera il player locale e verifica il suo NetId
	// -------------------------------------------------------
	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

	if (!LocalPlayer)
	{
		FDebugUtils::Error(TEXT("[JOIN] ERRORE: nessun LocalPlayer trovato! Impossibile avviare FindSessions."));
		// Pulisci il delegate che abbiamo appena registrato
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		return;
	}

	// Verifica che l'ID univoco di Steam sia valido (richiede Steam loggato)
	TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId().GetUniqueNetId();

	if (!PlayerId.IsValid())
	{
		FDebugUtils::Error(TEXT("[JOIN] ERRORE: UniqueNetId del player non valido!"));
		FDebugUtils::Warning(TEXT("[JOIN] Steam potrebbe non essere loggato o non aver completato l'autenticazione."));
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		return;
	}

	FDebugUtils::Info(FString::Printf(
		TEXT("[JOIN] Player NetId: %s. Avvio FindSessions() ASYNC..."),
		*PlayerId->ToString()
	));

	// -------------------------------------------------------
	// Avvia la ricerca (operazione ASINCRONA)
	// Il risultato arriverà in OnFindSessionsComplete()
	// -------------------------------------------------------
	const bool bStarted = OnlineSessionInterface->FindSessions(
		*LocalPlayer->GetPreferredUniqueNetId(),
		SessionSearch.ToSharedRef()
	);

	if (!bStarted)
	{
		FDebugUtils::Error(TEXT("[JOIN] ERRORE: FindSessions() ha restituito false immediatamente!"));
		FDebugUtils::Warning(TEXT("[JOIN] Cause possibili: Steam non attivo, già in una sessione, problema autenticazione."));
		// Nessuna callback arriverà: pulisci subito il delegate
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}
	else
	{
		FDebugUtils::Success(TEXT("[JOIN] FindSessions() ASYNC avviato. Attendo OnFindSessionsComplete..."));
	}
}


// ============================================================
//  CALLBACK: FIND SESSIONS COMPLETE
// ============================================================

void AMenuSystemCharacter::OnFindSessionsComplete(bool bWasSuccessful)
{
	FDebugUtils::Section(TEXT("FIND SESSIONS COMPLETE"));

	FDebugUtils::Log(
		FString::Printf(TEXT("[FIND CB] Ricerca completata | Risultato: %s"),
			bWasSuccessful ? TEXT("SUCCESSO") : TEXT("FALLITO")
		),
		bWasSuccessful ? FColor::Green : FColor::Red
	);

	// -------------------------------------------------------
	// BUG FIX #1 + #4: pulisci il FindDelegate subito.
	// Prima: handle non salvato -> Clear non faceva nulla -> doppi callback.
	// Ora: handle salvato correttamente in JoinGameSession().
	// -------------------------------------------------------
	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		FDebugUtils::Info(TEXT("[FIND CB] FindSessionsDelegate pulito (BUG FIX #1/#4)."));
	}
	else
	{
		FDebugUtils::Error(TEXT("[FIND CB] ERRORE: OnlineSessionInterface non valida durante la callback!"));
		return;
	}

	// --- Guard: la ricerca è fallita? ---
	if (!bWasSuccessful)
	{
		FDebugUtils::Error(TEXT("[FIND CB] La ricerca sessioni è fallita."));
		FDebugUtils::Warning(TEXT("[FIND CB] Controlla: 1) Steam attivo? 2) Connessione internet? 3) L'host usa SteamDevAppId=480?"));
		return;
	}

	// --- Guard: SessionSearch valida? ---
	if (!SessionSearch.IsValid())
	{
		FDebugUtils::Error(TEXT("[FIND CB] ERRORE: SessionSearch non valida dopo la ricerca!"));
		return;
	}

	// -------------------------------------------------------
	// Log numero risultati
	// -------------------------------------------------------
	const int32 NumResults = SessionSearch->SearchResults.Num();

	FDebugUtils::Log(
		FString::Printf(TEXT("[FIND CB] Sessioni trovate: %d"), NumResults),
		NumResults > 0 ? FColor::Green : FColor::Yellow
	);

	if (NumResults == 0)
	{
		FDebugUtils::Warning(TEXT("[FIND CB] Nessuna sessione disponibile."));
		FDebugUtils::Warning(TEXT("[FIND CB] Cause: 1) Host non ha ancora creato. 2) Firewall. 3) SteamDevAppId diverso. 4) Filtri Presence/Lobby non corrispondono."));
		return;
	}

	// -------------------------------------------------------
	// Itera su tutti i risultati: logga ognuno e cerca FreeForAll
	// -------------------------------------------------------
	for (int32 i = 0; i < SessionSearch->SearchResults.Num(); ++i)
	{
		const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[i];

		// Dati identificativi
		const FString SessionId  = Result.GetSessionIdStr();
		const FString OwnerName  = Result.Session.OwningUserName;
		const int32   Ping       = Result.PingInMs;
		const int32   OpenSlots  = Result.Session.NumOpenPublicConnections;
		const int32   MaxSlots   = Result.Session.SessionSettings.NumPublicConnections;

		// Recupera il MatchType custom impostato dall'host in CreateGameSession()
		FString MatchType;
		Result.Session.SessionSettings.Get(FName("MatchType"), MatchType);

		// Log dettagliato per ogni sessione trovata
		FDebugUtils::Info(FString::Printf(
			TEXT("[FIND CB] [%d/%d] Id='%s' | Host='%s' | Ping=%dms | Slot=%d/%d | MatchType='%s'"),
			i + 1, NumResults,
			*SessionId,
			*OwnerName,
			Ping,
			MaxSlots - OpenSlots, MaxSlots,
			MatchType.IsEmpty() ? TEXT("(non impostato)") : *MatchType
		));

		// -------------------------------------------------------
		// Controlla se questo è il tipo di partita che cerchiamo
		// -------------------------------------------------------
		if (MatchType != TEXT("FreeForAll"))
		{
			FDebugUtils::Info(FString::Printf(
				TEXT("[FIND CB] [%d] Sessione ignorata: MatchType='%s' (atteso: 'FreeForAll')"),
				i, *MatchType
			));
			continue;
		}

		// -------------------------------------------------------
		// MatchType corretto: verifica che ci siano slot disponibili
		// -------------------------------------------------------
		if (OpenSlots <= 0)
		{
			FDebugUtils::Warning(FString::Printf(
				TEXT("[FIND CB] [%d] Sessione FreeForAll trovata ma PIENA (0 slot liberi). Cerco la prossima..."),
				i
			));
			continue;
		}

		FDebugUtils::Success(FString::Printf(
			TEXT("[FIND CB] Sessione FreeForAll valida trovata! (index %d) Host='%s' | Ping=%dms | Slot liberi=%d"),
			i, *OwnerName, Ping, OpenSlots
		));

		// -------------------------------------------------------
		// BUG FIX #2: registra il JoinDelegate e SALVA l'handle.
		// Prima il risultato non veniva salvato in JoinSessionCompleteDelegateHandle,
		// rendendo impossibile pulire il delegate in OnJoinSessionsComplete().
		// -------------------------------------------------------
		JoinSessionCompleteDelegateHandle =
			OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

		FDebugUtils::Info(TEXT("[FIND CB] JoinSessionDelegate registrato (handle salvato - BUG FIX #2)."));

		// Recupera il player locale
		const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

		if (!LocalPlayer)
		{
			FDebugUtils::Error(TEXT("[FIND CB] ERRORE: LocalPlayer non trovato! Impossibile fare il join."));
			OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
			return;
		}

		// Verifica NetId
		TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId().GetUniqueNetId();
		if (!PlayerId.IsValid())
		{
			FDebugUtils::Error(TEXT("[FIND CB] ERRORE: UniqueNetId non valido! Impossibile fare il join."));
			OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
			return;
		}

		FDebugUtils::Info(FString::Printf(
			TEXT("[FIND CB] Avvio JoinSession() ASYNC verso l'host '%s'..."),
			*OwnerName
		));

		// -------------------------------------------------------
		// Avvia il join (operazione ASINCRONA)
		// Il risultato arriverà in OnJoinSessionsComplete()
		// -------------------------------------------------------
		const bool bJoinStarted = OnlineSessionInterface->JoinSession(
			*LocalPlayer->GetPreferredUniqueNetId(),
			NAME_GameSession,
			Result
		);

		if (!bJoinStarted)
		{
			FDebugUtils::Error(TEXT("[FIND CB] ERRORE: JoinSession() ha restituito false immediatamente!"));
			OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
		}
		else
		{
			FDebugUtils::Success(TEXT("[FIND CB] JoinSession() ASYNC avviato. Attendo OnJoinSessionsComplete..."));
		}

		// IMPORTANTE: esci dal loop dopo il primo join valido.
		// Non tentare di joinare più sessioni in parallelo.
		break;
	}

	FDebugUtils::Separator();
}


// ============================================================
//  CALLBACK: JOIN SESSION COMPLETE
// ============================================================

void AMenuSystemCharacter::OnJoinSessionsComplete(
	FName SessionName,
	EOnJoinSessionCompleteResult::Type Result)
{
	FDebugUtils::Section(TEXT("JOIN SESSION COMPLETE"));

	// -------------------------------------------------------
	// Converti l'enum Result in una stringa leggibile per il debug
	// -------------------------------------------------------
	FString ResultStr;
	switch (Result)
	{
		case EOnJoinSessionCompleteResult::Success:
			ResultStr = TEXT("Success");
			break;
		case EOnJoinSessionCompleteResult::SessionIsFull:
			ResultStr = TEXT("SessionIsFull");
			break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			ResultStr = TEXT("SessionDoesNotExist");
			break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			ResultStr = TEXT("CouldNotRetrieveAddress");
			break;
		case EOnJoinSessionCompleteResult::AlreadyInSession:
			ResultStr = TEXT("AlreadyInSession");
			break;
		case EOnJoinSessionCompleteResult::UnknownError:
		default:
			ResultStr = TEXT("UnknownError");
			break;
	}

	FDebugUtils::Log(
		FString::Printf(TEXT("[JOIN CB] Sessione '%s' | Risultato: %s"),
			*SessionName.ToString(), *ResultStr),
		Result == EOnJoinSessionCompleteResult::Success ? FColor::Green : FColor::Red
	);

	// -------------------------------------------------------
	// BUG FIX #2: pulisci il JoinDelegate con l'handle salvato.
	// Prima l'handle era vuoto: Clear non faceva nulla.
	// -------------------------------------------------------
	if (!OnlineSessionInterface.IsValid())
	{
		FDebugUtils::Error(TEXT("[JOIN CB] ERRORE: OnlineSessionInterface non valida durante la callback!"));
		return;
	}

	OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	FDebugUtils::Info(TEXT("[JOIN CB] JoinSessionDelegate pulito (BUG FIX #2)."));

	// -------------------------------------------------------
	// Gestione degli errori specifici con messaggi diagnostici
	// -------------------------------------------------------
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		FDebugUtils::Error(FString::Printf(TEXT("[JOIN CB] Join fallito: %s"), *ResultStr));

		switch (Result)
		{
			case EOnJoinSessionCompleteResult::SessionIsFull:
				FDebugUtils::Warning(TEXT("[JOIN CB] La sessione è piena. Tutti gli slot sono occupati. Riprova più tardi."));
				break;
			case EOnJoinSessionCompleteResult::SessionDoesNotExist:
				FDebugUtils::Warning(TEXT("[JOIN CB] La sessione non esiste più. L'host potrebbe aver chiuso la partita."));
				break;
			case EOnJoinSessionCompleteResult::AlreadyInSession:
				FDebugUtils::Warning(TEXT("[JOIN CB] Sei già in questa sessione. Nessuna azione necessaria."));
				break;
			case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
				FDebugUtils::Warning(TEXT("[JOIN CB] Impossibile risolvere l'indirizzo del server."));
				FDebugUtils::Warning(TEXT("[JOIN CB] Cause possibili: NAT traversal fallito, firewall, o sessione già chiusa."));
				break;
			default:
				FDebugUtils::Warning(TEXT("[JOIN CB] Errore sconosciuto. Controlla il log per dettagli Steam."));
				break;
		}

		return;
	}

	// -------------------------------------------------------
	// Join riuscito: risolvi l'indirizzo del server
	// GetResolvedConnectString() restituisce l'IP (o Steam ID relay)
	// nel formato usabile da ClientTravel()
	// -------------------------------------------------------
	FString Address;

	if (!OnlineSessionInterface->GetResolvedConnectString(SessionName, Address))
	{
		FDebugUtils::Error(TEXT("[JOIN CB] ERRORE: GetResolvedConnectString() fallito!"));
		FDebugUtils::Warning(TEXT("[JOIN CB] Cause possibili: relay Steam non disponibile, NAT non attraversato, sessione già chiusa."));
		return;
	}

	FDebugUtils::Success(FString::Printf(
		TEXT("[JOIN CB] Indirizzo server risolto: '%s'. Avvio ClientTravel..."),
		*Address
	));

	// -------------------------------------------------------
	// Recupera il PlayerController e avvia il travel verso il server
	// -------------------------------------------------------
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();

	if (!PC)
	{
		FDebugUtils::Error(TEXT("[JOIN CB] ERRORE: PlayerController non trovato! Impossibile eseguire ClientTravel."));
		return;
	}

	// TRAVEL_Absolute = indirizzo assoluto (include Steam ID / IP completo)
	// Necessario per indirizzi Steam relay che non sono URL relativi
	PC->ClientTravel(Address, TRAVEL_Absolute);

	FDebugUtils::Success(FString::Printf(
		TEXT("[JOIN CB] ClientTravel avviato verso: '%s'"),
		*Address
	));

	FDebugUtils::Separator();
}


// ============================================================
//  INPUT HANDLERS
// ============================================================

/**
 * Riceve il valore Vector2D dal MoveAction e lo smista a DoMove().
 * X = destra/sinistra (Right), Y = avanti/indietro (Forward).
 */
void AMenuSystemCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

/**
 * Riceve il valore Vector2D da LookAction o MouseLookAction e lo smista a DoLook().
 * X = yaw (orizzontale), Y = pitch (verticale).
 */
void AMenuSystemCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

/**
 * Applica il movimento in avanti/indietro e destra/sinistra
 * calcolato relativo alla rotazione yaw del controller (camera).
 */
void AMenuSystemCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// Calcola la direzione avanti e destra dal yaw del controller
		const FRotator Rotation    = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection,   Right);
	}
}

/**
 * Applica la rotazione yaw e pitch al controller del personaggio.
 * La camera segue il controller tramite il CameraBoom (bUsePawnControlRotation=true).
 */
void AMenuSystemCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

/** Avvia il salto tramite ACharacter::Jump(). */
void AMenuSystemCharacter::DoJumpStart()
{
	Jump();
}

/** Termina il salto tramite ACharacter::StopJumping(). */
void AMenuSystemCharacter::DoJumpEnd()
{
	StopJumping();
}