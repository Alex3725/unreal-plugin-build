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
#include "OnlineSessionSettings.h"

// ============================================================
// HELPER MACRO: stampa a schermo + log contemporaneamente.
// Uso: DBG_MSG(FColor::Green, TEXT("messaggio"))
// ============================================================
#define DBG_MSG(Color, Msg) \
if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 8.f, Color, Msg); } \
UE_LOG(LogMenuSystem, Log, TEXT("%s"), *FString(Msg))

#define DBG_ERR(Msg) \
if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, Msg); } \
UE_LOG(LogMenuSystem, Error, TEXT("%s"), *FString(Msg))

#define DBG_WARN(Msg) \
if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Yellow, Msg); } \
UE_LOG(LogMenuSystem, Warning, TEXT("%s"), *FString(Msg))

// =========================
// COSTRUTTORE
// =========================

AMenuSystemCharacter::AMenuSystemCharacter():

	// ---------------------------------------------------------
	// BUG FIX #3 (parziale): tutti e quattro i delegate vanno
	// inizializzati qui. Prima DestroySessionCompleteDelegate
	// NON era presente in questa lista, quindi quando veniva
	// chiamato DestroySession la callback non scattava mai
	// e la sessione non veniva mai ricreata.
	// ---------------------------------------------------------

	// Quando la creazione della sessione multiplayer è finita, chiama questa funzione
	CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this, &AMenuSystemCharacter::OnCreateSessionComplete)),

	// Quando la distruzione della sessione è finita, chiama questa funzione
	// (necessario per ricreare la sessione dopo averla distrutta)
	DestroySessionCompleteDelegate(FOnDestroySessionCompleteDelegate::CreateUObject(this, &AMenuSystemCharacter::OnDestroySessionComplete)),

	// Quando FindSessions finisce, chiama OnFindSessionsComplete
	FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete)),

	// Quando JoinSession finisce, chiama OnJoinSessionsComplete
	JoinSessionCompleteDelegate(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionsComplete))

{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)

	// Reset del flag per la ricreazione sessione
	bCreateSessionOnDestroy = false;
	
	// =========================
	// ONLINE SUBSYSTEM INIT
	// =========================

	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();

	if (Subsystem)
	{
		OnlineSessionInterface = Subsystem->GetSessionInterface();

		// Conferma a schermo che il subsistema è stato trovato
		DBG_MSG(FColor::Green,
			FString::Printf(TEXT("[INIT] Subsistema online trovato: %s"),
			*Subsystem->GetSubsystemName().ToString()));

		// Verifica anche che l'interfaccia sessione sia valida
		if (OnlineSessionInterface.IsValid())
		{
			DBG_MSG(FColor::Green, TEXT("[INIT] OnlineSessionInterface valida."));
		}
		else
		{
			DBG_ERR(TEXT("[INIT] ERRORE: OnlineSessionInterface NON valida dopo Get()!"));
		}
	}
	else
	{
		// Nessun subsistema trovato: probabilmente Steam non è avviato
		// oppure il plugin OnlineSubsystemSteam non è abilitato
		DBG_ERR(TEXT("[INIT] ERRORE CRITICO: Nessun subsistema online trovato!"));
		DBG_WARN(TEXT("[INIT] Assicurati che Steam sia avviato e che il plugin OnlineSubsystemSteam sia abilitato nel .uproject"));
	}
}

// =========================
// INPUT
// =========================

void AMenuSystemCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Look);
	}
	else
	{
		UE_LOG(LogMenuSystem, Error,
			TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system."),
			*GetNameSafe(this));
	}
}

// =========================
// CREATE SESSION
// =========================

void AMenuSystemCharacter::CreateGameSession()
{
	DBG_MSG(FColor::Cyan, TEXT("[CREATE] CreateGameSession() chiamata."));

	// Controllo di sicurezza: l'interfaccia online deve essere valida
	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[CREATE] ERRORE: OnlineSessionInterface non valida. Impossibile creare la sessione."));
		return;
	}

	// -------------------------------------------------------
	// Controlla se esiste già una sessione attiva
	// -------------------------------------------------------
	auto ExistingSession = OnlineSessionInterface->GetNamedSession(NAME_GameSession);

	if (ExistingSession != nullptr)
	{
		// Sessione già esistente: la distruggiamo prima di ricrearne una nuova.
		// Settiamo il flag così OnDestroySessionComplete sa che deve ricrearla.
		DBG_WARN(TEXT("[CREATE] Sessione esistente trovata. La distruggo prima di crearne una nuova..."));

		bCreateSessionOnDestroy = true;

		// ---------------------------------------------------------
		// BUG FIX #3 (completo): registriamo il DestroyDelegate
		// e SALVIAMO l'handle. Prima questo blocco non esisteva,
		// quindi dopo DestroySession il flusso si bloccava qui.
		// ---------------------------------------------------------
		DestroySessionCompleteDelegateHandle =
			OnlineSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegate);

		OnlineSessionInterface->DestroySession(NAME_GameSession);

		// Usciamo: la sessione verrà ricreata in OnDestroySessionComplete
		return;
	}

	// -------------------------------------------------------
	// Nessuna sessione esistente: procedi con la creazione
	// -------------------------------------------------------

	// Registra il delegate per la callback di creazione e salva l'handle
	CreateSessionCompleteDelegateHandle =
		OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);

	DBG_MSG(FColor::Cyan, TEXT("[CREATE] Delegate di creazione registrato. Configuro le impostazioni sessione..."));

	// Configurazione sessione
	TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShared<FOnlineSessionSettings>();

	SessionSettings->bIsLANMatch             = false;   // sessione online, non LAN
	SessionSettings->NumPublicConnections    = 4;       // max 4 giocatori
	SessionSettings->bShouldAdvertise        = true;    // visibile nei risultati di ricerca
	SessionSettings->bUsesPresence           = true;    // richiesto da Steam per le lobby
	SessionSettings->bAllowJoinInProgress    = true;    // si può entrare anche a partita iniziata
	SessionSettings->bAllowJoinViaPresence   = true;    // join tramite presenza Steam
	SessionSettings->bUseLobbiesIfAvailable  = true;    // usa il sistema Lobby di Steam se disponibile
	SessionSettings->bAllowInvites           = true;    // permetti gli inviti
	SessionSettings->bAllowJoinViaPresenceFriendsOnly = false; // visibile a tutti, non solo amici

	// Imposta il tipo di partita come chiave di ricerca
	// "Questa sessione ha MatchType = FreeForAll e deve essere visibile agli altri giocatori"
	// FName("MatchType") è la chiave, FString("FreeForAll") è il valore
	SessionSettings->Set(
		FName("MatchType"),
		FString("FreeForAll"),
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing
	);

	DBG_MSG(FColor::Cyan, FString::Printf(
		TEXT("[CREATE] Settings: LAN=%s | Connections=%d | Advertise=%s | Presence=%s | Lobbies=%s"),
		SessionSettings->bIsLANMatch ? TEXT("SI") : TEXT("NO"),
		SessionSettings->NumPublicConnections,
		SessionSettings->bShouldAdvertise ? TEXT("SI") : TEXT("NO"),
		SessionSettings->bUsesPresence ? TEXT("SI") : TEXT("NO"),
		SessionSettings->bUseLobbiesIfAvailable ? TEXT("SI") : TEXT("NO")
	));

	// Recupera il player locale
	ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

	if (!LocalPlayer)
	{
		DBG_ERR(TEXT("[CREATE] ERRORE: Nessun LocalPlayer trovato! Impossibile creare la sessione."));
		return;
	}

	DBG_MSG(FColor::Cyan, FString::Printf(
		TEXT("[CREATE] LocalPlayer trovato. ControllerId=%d. Avvio creazione sessione..."),
		LocalPlayer->GetControllerId()
	));

	// Avvia la creazione della sessione (ASYNC)
	// Il risultato arriva in OnCreateSessionComplete
	bool bStarted = OnlineSessionInterface->CreateSession(
		LocalPlayer->GetControllerId(),
		NAME_GameSession,
		*SessionSettings
	);

	if (!bStarted)
	{
		// CreateSession ha ritornato false immediatamente (errore sincrono)
		DBG_ERR(TEXT("[CREATE] ERRORE: CreateSession() ha ritornato false immediatamente!"));
		DBG_WARN(TEXT("[CREATE] Possibili cause: Steam non avviato, sessione già in creazione, o SteamDevAppId errato nel DefaultEngine.ini"));

		// Pulisci il delegate visto che non ci sarà callback
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	}
	else
	{
		DBG_MSG(FColor::Cyan, TEXT("[CREATE] CreateSession() avviato con successo. In attesa del callback..."));
	}
}

// =========================
// CALLBACK: DESTROY SESSION COMPLETE
// =========================

void AMenuSystemCharacter::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	// ---------------------------------------------------------
	// BUG FIX #3 (implementazione mancante):
	// Questa funzione era DICHIARATA nell'header ma non aveva
	// alcuna implementazione nel .cpp. Di conseguenza, dopo
	// DestroySession il flusso si bloccava completamente
	// e la sessione non veniva mai ricreata.
	// ---------------------------------------------------------

	DBG_MSG(bWasSuccessful ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[DESTROY] OnDestroySessionComplete | Sessione: %s | Successo: %s"),
		*SessionName.ToString(),
		bWasSuccessful ? TEXT("SI") : TEXT("NO")
	));

	// Pulisci sempre il delegate, indipendentemente dal risultato
	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		DBG_MSG(FColor::Green, TEXT("[DESTROY] DestroyDelegate pulito."));
	}

	if (bWasSuccessful)
	{
		// Controlla se è richiesta la ricreazione della sessione
		if (bCreateSessionOnDestroy)
		{
			DBG_MSG(FColor::Cyan, TEXT("[DESTROY] Sessione distrutta con successo. Riciclo: richiamo CreateGameSession()..."));
			bCreateSessionOnDestroy = false;
			CreateGameSession();
		}
		else
		{
			DBG_MSG(FColor::White, TEXT("[DESTROY] Sessione distrutta. Nessuna ricreazione richiesta."));
		}
	}
	else
	{
		DBG_ERR(TEXT("[DESTROY] ERRORE: la distruzione della sessione è fallita! Impossibile procedere."));
		bCreateSessionOnDestroy = false;
	}
}

// =========================
// CALLBACK: CREATE SESSION COMPLETE
// =========================

void AMenuSystemCharacter::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	DBG_MSG(bWasSuccessful ? FColor::Cyan : FColor::Red,
		FString::Printf(TEXT("[CREATE CB] Risultato: %s | Sessione: %s"),
		bWasSuccessful ? TEXT("SUCCESS") : TEXT("FAIL"),
		*SessionName.ToString()
	));

	// Pulisci il delegate di creazione appena possibile
	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		DBG_MSG(FColor::Green, TEXT("[CREATE CB] CreateDelegate pulito."));
	}

	if (!bWasSuccessful)
	{
		DBG_ERR(TEXT("[CREATE CB] ERRORE: la sessione non è stata creata."));
		DBG_WARN(TEXT("[CREATE CB] Controlla: 1) Steam è avviato? 2) SteamDevAppId=480 in DefaultEngine.ini? 3) bEnabled=true per OnlineSubsystemSteam?"));
		return;
	}

	// Sessione creata con successo: vai nella mappa Lobby come Listen Server
	UWorld* World = GetWorld();

	if (World)
	{
		DBG_MSG(FColor::Cyan, TEXT("[CREATE CB] Sessione creata! Eseguo ServerTravel verso /Game/ThirdPerson/Maps/Lobby?listen ..."));

		// ?listen = apre questa mappa come listen server (host che gioca anche)
		// C:\...\Content\ corrisponde a /Game/
		World->ServerTravel(FString("/Game/ThirdPerson/Maps/Lobby?listen"));
	}
	else
	{
		DBG_ERR(TEXT("[CREATE CB] ERRORE: GetWorld() ha ritornato nullptr! ServerTravel non eseguito."));
	}
}

// =========================
// JOIN GAME SESSION
// =========================

void AMenuSystemCharacter::JoinGameSession()
{
	DBG_MSG(FColor::Orange, TEXT("[JOIN] JoinGameSession() chiamata. Avvio ricerca sessioni..."));

	// STEP 1: Verifica che il sistema online sia valido
	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: OnlineSessionInterface non valida. Impossibile cercare sessioni."));
		return;
	}

	// STEP 2: Controlla se è già in corso una ricerca per evitare doppioni
	if (SessionSearch.IsValid() && SessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		DBG_WARN(TEXT("[JOIN] ATTENZIONE: una ricerca sessioni è già in corso. Ignoro la nuova richiesta."));
		return;
	}

	// ---------------------------------------------------------
	// BUG FIX #1: prima il risultato di AddOnFindSessionsCompleteDelegate_Handle
	// non veniva salvato in FindSessionsCompleteDelegateHandle,
	// rendendo impossibile rimuovere il delegate in seguito.
	// ---------------------------------------------------------

	// STEP 3: Registra il delegate e SALVA l'handle (BUG FIX)
	FindSessionsCompleteDelegateHandle =
		OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);

	DBG_MSG(FColor::Orange, TEXT("[JOIN] FindSessionsDelegate registrato."));

	// STEP 4: Crea l'oggetto di ricerca sessioni
	SessionSearch = MakeShareable(new FOnlineSessionSearch());

	// STEP 5: Configurazione parametri di ricerca
	// Valore alto perché con SteamDevAppId=480 ci sono molte sessioni pubbliche di test
	SessionSearch->MaxSearchResults = 10000;
	SessionSearch->bIsLanQuery      = false; // online, non LAN

	// Filtro presenza: cerca solo sessioni con presenza attiva (richiesto da Steam)
	// NOTA: usiamo FName(TEXT("SEARCH_PRESENCE")) invece della macro SEARCH_PRESENCE
	// perché su alcune versioni di UE5 la macro non viene risolta correttamente
	// a causa di un problema di inclusione degli header di OnlineSessionNames.
	SessionSearch->QuerySettings.Set(
		FName(TEXT("SEARCH_PRESENCE")),
		true,
		EOnlineComparisonOp::Equals
	);

	// Filtro lobby: cerca solo sessioni che usano il sistema Lobby di Steam
	// (coerente con bUseLobbiesIfAvailable=true impostato in CreateGameSession)
	SessionSearch->QuerySettings.Set(
		SEARCH_LOBBIES,
		true,
		EOnlineComparisonOp::Equals
	);

	DBG_MSG(FColor::Orange, FString::Printf(
		TEXT("[JOIN] Parametri ricerca: MaxResults=%d | LAN=%s | Presence=true | Lobbies=true"),
		SessionSearch->MaxSearchResults,
		SessionSearch->bIsLanQuery ? TEXT("SI") : TEXT("NO")
	));

	// STEP 6: Recupera il player locale
	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	
	if (!LocalPlayer)
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: Nessun LocalPlayer trovato! Impossibile avviare FindSessions."));

		// Pulisci il delegate visto che non procederemo
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		return;
	}

	// Verifica che il NetId del player sia valido prima di usarlo
	TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId().GetUniqueNetId();

	if (!PlayerId.IsValid())
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: UniqueNetId del player non valido! Steam potrebbe non essere loggato."));
		DBG_WARN(TEXT("[JOIN] Assicurati di essere loggato su Steam con un account valido."));

		// Pulisci il delegate
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		return;
	}

	DBG_MSG(FColor::Orange, FString::Printf(
		TEXT("[JOIN] Player NetId: %s. Avvio FindSessions (ASYNC)..."),
		*PlayerId->ToString()
	));

	// STEP 7: Avvia la ricerca (ASYNC)
	// Il risultato arriverà in OnFindSessionsComplete
	bool bStarted = OnlineSessionInterface->FindSessions(
		*LocalPlayer->GetPreferredUniqueNetId(),
		SessionSearch.ToSharedRef()
	);

	if (!bStarted)
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: FindSessions() ha ritornato false immediatamente!"));
		DBG_WARN(TEXT("[JOIN] Possibili cause: Steam non avviato, già in una sessione, o problema di autenticazione."));

		// Pulisci il delegate visto che non ci sarà callback
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}
	else
	{
		DBG_MSG(FColor::Orange, TEXT("[JOIN] FindSessions() avviato. In attesa del callback OnFindSessionsComplete..."));
	}
}

// =========================
// CALLBACK: FIND SESSIONS COMPLETE
// =========================

void AMenuSystemCharacter::OnFindSessionsComplete(bool bWasSuccessful)
{
	DBG_MSG(bWasSuccessful ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[FIND CB] OnFindSessionsComplete | Successo: %s"),
		bWasSuccessful ? TEXT("SI") : TEXT("NO")
	));

	// ---------------------------------------------------------
	// BUG FIX #1 (pulizia): ora che abbiamo l'handle salvato,
	// possiamo rimuovere correttamente il delegate.
	// Prima l'handle era vuoto e la Clear non faceva nulla.
	// ---------------------------------------------------------

	// Pulisci subito il FindSessions delegate per evitare doppi callback
	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		DBG_MSG(FColor::Green, TEXT("[FIND CB] FindDelegate pulito."));
	}
	else
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: OnlineSessionInterface non valida durante la callback!"));
		return;
	}

	// Caso: la ricerca è fallita
	if (!bWasSuccessful)
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: la ricerca sessioni è fallita."));
		DBG_WARN(TEXT("[FIND CB] Controlla: 1) Steam attivo? 2) Connessione internet? 3) Host usa lo stesso SteamDevAppId=480?"));
		return;
	}

	// Caso: SessionSearch non è valida
	if (!SessionSearch.IsValid())
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: SessionSearch non è valida dopo la ricerca!"));
		return;
	}

	// Log numero sessioni trovate
	int32 NumResults = SessionSearch->SearchResults.Num();
	DBG_MSG(NumResults > 0 ? FColor::Green : FColor::Yellow,
		FString::Printf(TEXT("[FIND CB] Sessioni trovate: %d"), NumResults)
	);

	// Caso: nessuna sessione trovata
	if (NumResults == 0)
	{
		DBG_WARN(TEXT("[FIND CB] Nessuna sessione disponibile."));
		DBG_WARN(TEXT("[FIND CB] Possibili cause: 1) L'host non ha ancora creato la sessione. 2) Firewall. 3) Diverso SteamDevAppId. 4) bUseLobbiesIfAvailable non corrisponde."));
		return;
	}

	// -------------------------------------------------------
	// Itera su tutte le sessioni trovate e cerca MatchType == "FreeForAll"
	// -------------------------------------------------------
	for (int32 i = 0; i < SessionSearch->SearchResults.Num(); ++i)
	{
		const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[i];

		// Dati identificativi della sessione
		const FString SessionId  = Result.GetSessionIdStr();
		const FString OwnerName  = Result.Session.OwningUserName;
		const int32   Ping       = Result.PingInMs;
		const int32   OpenSlots  = Result.Session.NumOpenPublicConnections;

		// Recupera il MatchType custom impostato dall'host
		FString MatchType;
		Result.Session.SessionSettings.Get(FName("MatchType"), MatchType);

		DBG_MSG(FColor::Cyan, FString::Printf(
			TEXT("[FIND CB] [%d/%d] Id=%s | Host=%s | Ping=%dms | SlotsLiberi=%d | MatchType=%s"),
			i + 1,
			NumResults,
			*SessionId,
			*OwnerName,
			Ping,
			OpenSlots,
			MatchType.IsEmpty() ? TEXT("(non impostato)") : *MatchType
		));

		// Controlla se questa sessione è del tipo giusto
		if (MatchType == TEXT("FreeForAll"))
		{
			DBG_MSG(FColor::Green, FString::Printf(
				TEXT("[FIND CB] Sessione FreeForAll trovata! (index %d) Tentativo di join..."),
				i
			));

			// Verifica che ci siano slot liberi prima di tentare il join
			if (OpenSlots <= 0)
			{
				DBG_WARN(TEXT("[FIND CB] La sessione è piena (0 slot liberi). Cerco la prossima..."));
				continue;
			}

			// ---------------------------------------------------------
			// BUG FIX #2: prima il risultato di AddOnJoinSessionCompleteDelegate_Handle
			// non veniva salvato in JoinSessionCompleteDelegateHandle.
			// Ora lo salviamo correttamente così possiamo pulirlo in seguito.
			// ---------------------------------------------------------

			// Registra il JoinDelegate e SALVA l'handle (BUG FIX)
			JoinSessionCompleteDelegateHandle =
				OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

			DBG_MSG(FColor::Green, TEXT("[FIND CB] JoinDelegate registrato."));

			// Recupera il player locale
			const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

			if (!LocalPlayer)
			{
				DBG_ERR(TEXT("[FIND CB] ERRORE: LocalPlayer non trovato! Impossibile fare il join."));

				// Pulisci il delegate appena registrato
				OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
				return;
			}

			// Verifica NetId
			TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId().GetUniqueNetId();
			if (!PlayerId.IsValid())
			{
				DBG_ERR(TEXT("[FIND CB] ERRORE: UniqueNetId non valido! Impossibile fare il join."));
				OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
				return;
			}

			DBG_MSG(FColor::Green, FString::Printf(
				TEXT("[FIND CB] Avvio JoinSession per l'host: %s..."),
				*OwnerName
			));

			// Avvia il join (ASYNC)
			// Il risultato arriverà in OnJoinSessionsComplete
			bool bJoinStarted = OnlineSessionInterface->JoinSession(
				*LocalPlayer->GetPreferredUniqueNetId(),
				NAME_GameSession,
				Result
			);

			if (!bJoinStarted)
			{
				DBG_ERR(TEXT("[FIND CB] ERRORE: JoinSession() ha ritornato false immediatamente!"));
				OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
			}
			else
			{
				DBG_MSG(FColor::Green, TEXT("[FIND CB] JoinSession() avviato. In attesa del callback OnJoinSessionsComplete..."));
			}

			// 🔥 IMPORTANTE: esci dal loop dopo il primo join valido
			// per non tentare di joinare più sessioni in parallelo
			break;
		}
		else
		{
			// MatchType non corrisponde, continua a cercare
			DBG_MSG(FColor::White, FString::Printf(
				TEXT("[FIND CB] [%d] Sessione ignorata: MatchType='%s' (atteso 'FreeForAll')"),
				i, *MatchType
			));
		}
	}
}

// =========================
// CALLBACK: JOIN SESSION COMPLETE
// =========================

void AMenuSystemCharacter::OnJoinSessionsComplete(
	FName SessionName,
	EOnJoinSessionCompleteResult::Type Result)
{
	// Mappa il risultato enum in una stringa leggibile per il debug
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

	DBG_MSG(Result == EOnJoinSessionCompleteResult::Success ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[JOIN CB] OnJoinSessionsComplete | Sessione: %s | Risultato: %s"),
		*SessionName.ToString(),
		*ResultStr
	));

	// ---------------------------------------------------------
	// BUG FIX #2 (pulizia): ora che abbiamo l'handle salvato,
	// possiamo rimuovere correttamente il JoinDelegate.
	// Prima l'handle era vuoto e la Clear non faceva nulla.
	// ---------------------------------------------------------

	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: OnlineSessionInterface non valida durante la callback!"));
		return;
	}

	// Pulisci il JoinDelegate
	OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	DBG_MSG(FColor::Green, TEXT("[JOIN CB] JoinDelegate pulito."));

	// Gestisci i casi di errore specifici
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		DBG_ERR(FString::Printf(TEXT("[JOIN CB] ERRORE nel join: %s"), *ResultStr));

		if (Result == EOnJoinSessionCompleteResult::SessionIsFull)
			{DBG_WARN(TEXT("[JOIN CB] La sessione è piena. Riprova più tardi."));}
		
		else if (Result == EOnJoinSessionCompleteResult::SessionDoesNotExist)
			{DBG_WARN(TEXT("[JOIN CB] La sessione non esiste più. L'host potrebbe aver chiuso la partita."));}
		
		else if (Result == EOnJoinSessionCompleteResult::AlreadyInSession)
			{DBG_WARN(TEXT("[JOIN CB] Sei già in questa sessione."));}
		
		else if (Result == EOnJoinSessionCompleteResult::CouldNotRetrieveAddress)
			{DBG_WARN(TEXT("[JOIN CB] Impossibile ottenere l'indirizzo del server. Problema di rete o NAT?"));}

		return;
	}

	// -------------------------------------------------------
	// Join riuscito: risolvi l'indirizzo e connettiti al server
	// -------------------------------------------------------

	FString Address;
	if (!OnlineSessionInterface->GetResolvedConnectString(SessionName, Address))
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: GetResolvedConnectString() fallito! Impossibile ottenere l'IP del server."));
		DBG_WARN(TEXT("[JOIN CB] Possibili cause: problema Steam relay, NAT traversal fallito, o sessione già chiusa."));
		return;
	}

	DBG_MSG(FColor::Green, FString::Printf(
		TEXT("[JOIN CB] Indirizzo server risolto: %s. Avvio ClientTravel..."),
		*Address
	));

	// Recupera il PlayerController per eseguire il travel
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();

	if (!PC)
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: PlayerController non trovato! Impossibile eseguire ClientTravel."));
		return;
	}

	// Viaggio verso il server dell'host
	// TRAVEL_Absolute = indirizzo assoluto (incluso IP Steam)
	PC->ClientTravel(Address, TRAVEL_Absolute);

	DBG_MSG(FColor::Green, FString::Printf(
		TEXT("[JOIN CB] ClientTravel avviato verso: %s"),
		*Address
	));
}

// =========================
// INPUT HANDLERS
// =========================

void AMenuSystemCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AMenuSystemCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AMenuSystemCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AMenuSystemCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AMenuSystemCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AMenuSystemCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}