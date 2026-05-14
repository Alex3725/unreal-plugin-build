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
// BUG FIX #5 (HOST): aggiunto GameplayStatics per OpenLevel.
// ServerTravel NON va usato per aprire la mappa lobby
// dal menu principale: in build standalone non crea
// correttamente il listen server né inizializza SteamNetDriver.
// OpenLevel con opzione "listen" è il metodo corretto.
// ============================================================
#include "Kismet/GameplayStatics.h"

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
	JoinSessionCompleteDelegate(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionsComplete)),

	// ---------------------------------------------------------
	// BUG FIX #6: delegate per gli inviti Steam.
	// Quando l'utente accetta un invito dall'overlay Steam,
	// questo delegate viene chiamato automaticamente dal sistema.
	// Senza di esso, accettare un invito non fa assolutamente nulla.
	// ---------------------------------------------------------
	SessionUserInviteAcceptedDelegate(FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::OnSessionUserInviteAccepted))

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

	// Reset del flag per la ricreazione sessione
	bCreateSessionOnDestroy = false;
	
	// =========================
	// ONLINE SUBSYSTEM INIT
	// =========================

	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();

	if (Subsystem)
	{
		OnlineSessionInterface = Subsystem->GetSessionInterface();

		DBG_MSG(FColor::Green,
			FString::Printf(TEXT("[INIT] Subsistema online trovato: %s"),
			*Subsystem->GetSubsystemName().ToString()));

		if (OnlineSessionInterface.IsValid())
		{
			DBG_MSG(FColor::Green, TEXT("[INIT] OnlineSessionInterface valida."));

			// ---------------------------------------------------------
			// BUG FIX #6: registra il delegate per gli inviti Steam.
			// Questo va fatto una sola volta all'avvio, qui nel costruttore.
			// L'handle viene salvato per poter rimuovere il delegate.
			// ---------------------------------------------------------
			SessionUserInviteAcceptedDelegateHandle =
				OnlineSessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
					SessionUserInviteAcceptedDelegate
				);

			DBG_MSG(FColor::Green, TEXT("[INIT] InviteAcceptedDelegate registrato."));
		}
		else
		{
			DBG_ERR(TEXT("[INIT] ERRORE: OnlineSessionInterface NON valida dopo Get()!"));
		}
	}
	else
	{
		DBG_ERR(TEXT("[INIT] ERRORE CRITICO: Nessun subsistema online trovato!"));
		DBG_WARN(TEXT("[INIT] Assicurati che Steam sia avviato e che il plugin OnlineSubsystemSteam sia abilitato nel .uproject"));
	}
}

// =========================
// INPUT
// =========================

void AMenuSystemCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
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
			TEXT("'%s' Failed to find an Enhanced Input component!"),
			*GetNameSafe(this));
	}
}

// =========================
// CREATE SESSION
// =========================

void AMenuSystemCharacter::CreateGameSession()
{
	DBG_MSG(FColor::Cyan, TEXT("[CREATE] CreateGameSession() chiamata."));

	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[CREATE] ERRORE: OnlineSessionInterface non valida."));
		return;
	}

	auto ExistingSession = OnlineSessionInterface->GetNamedSession(NAME_GameSession);

	if (ExistingSession != nullptr)
	{
		DBG_WARN(TEXT("[CREATE] Sessione esistente trovata. La distruggo prima di crearne una nuova..."));

		bCreateSessionOnDestroy = true;

		DestroySessionCompleteDelegateHandle =
			OnlineSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegate);

		OnlineSessionInterface->DestroySession(NAME_GameSession);
		return;
	}

	CreateSessionCompleteDelegateHandle =
		OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);

	DBG_MSG(FColor::Cyan, TEXT("[CREATE] Delegate registrato. Configuro le impostazioni sessione..."));

	TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShared<FOnlineSessionSettings>();

	SessionSettings->bIsLANMatch             = false;
	SessionSettings->NumPublicConnections    = 4;
	SessionSettings->bShouldAdvertise        = true;
	SessionSettings->bUsesPresence           = true;
	SessionSettings->bAllowJoinInProgress    = true;
	SessionSettings->bAllowJoinViaPresence   = true;
	SessionSettings->bUseLobbiesIfAvailable  = true;
	SessionSettings->bAllowInvites           = true;
	SessionSettings->bAllowJoinViaPresenceFriendsOnly = false;

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

	ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

	if (!LocalPlayer)
	{
		DBG_ERR(TEXT("[CREATE] ERRORE: Nessun LocalPlayer trovato!"));
		return;
	}

	DBG_MSG(FColor::Cyan, FString::Printf(
		TEXT("[CREATE] LocalPlayer trovato. ControllerId=%d. Avvio creazione sessione..."),
		LocalPlayer->GetControllerId()
	));

	bool bStarted = OnlineSessionInterface->CreateSession(
		LocalPlayer->GetControllerId(),
		NAME_GameSession,
		*SessionSettings
	);

	if (!bStarted)
	{
		DBG_ERR(TEXT("[CREATE] ERRORE: CreateSession() ha ritornato false immediatamente!"));
		DBG_WARN(TEXT("[CREATE] Controlla: Steam avviato? SteamDevAppId=480? bEnabled=true?"));
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
	// BUG FIX #3: questa funzione era DICHIARATA nell'header
	// ma non aveva implementazione nel .cpp originale.
	// ---------------------------------------------------------

	DBG_MSG(bWasSuccessful ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[DESTROY] OnDestroySessionComplete | Sessione: %s | Successo: %s"),
		*SessionName.ToString(),
		bWasSuccessful ? TEXT("SI") : TEXT("NO")
	));

	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		DBG_MSG(FColor::Green, TEXT("[DESTROY] DestroyDelegate pulito."));
	}

	if (bWasSuccessful)
	{
		if (bCreateSessionOnDestroy)
		{
			DBG_MSG(FColor::Cyan, TEXT("[DESTROY] Sessione distrutta. Riciclo: richiamo CreateGameSession()..."));
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
		DBG_ERR(TEXT("[DESTROY] ERRORE: la distruzione della sessione è fallita!"));
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

	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		DBG_MSG(FColor::Green, TEXT("[CREATE CB] CreateDelegate pulito."));
	}

	if (!bWasSuccessful)
	{
		DBG_ERR(TEXT("[CREATE CB] ERRORE: la sessione non è stata creata."));
		DBG_WARN(TEXT("[CREATE CB] Controlla: 1) Steam è avviato? 2) SteamDevAppId=480? 3) bEnabled=true per OnlineSubsystemSteam?"));
		return;
	}

	// -------------------------------------------------------
	// BUG FIX #5 (HOST): sostituito ServerTravel con OpenLevel.
	//
	// PROBLEMA PRECEDENTE:
	//   World->ServerTravel("/Game/ThirdPerson/Maps/Lobby?listen")
	//   In build standalone il listen server non viene creato
	//   correttamente e SteamNetDriver non si inizializza.
	//
	// SOLUZIONE CORRETTA:
	//   UGameplayStatics::OpenLevel con opzione "listen".
	//   Metodo standard per aprire una mappa come listen server
	//   dal main menu, sia in editor che in build standalone.
	//
	// QUANDO USARE ServerTravel:
	//   Solo quando il server è GIA' attivo e vuoi spostare
	//   TUTTI i client su un'altra mappa (es: Lobby -> GameMap).
	// -------------------------------------------------------

	DBG_MSG(FColor::Cyan, TEXT("[CREATE CB] Sessione creata! Avvio OpenLevel verso Lobby come listen server..."));

	UGameplayStatics::OpenLevel(
		this,
		FName("/Game/ThirdPerson/Maps/Lobby"),
		true,             // bAbsolute: usa percorso assoluto
		FString("listen") // apre come listen server
	);

	DBG_MSG(FColor::Cyan, TEXT("[CREATE CB] OpenLevel(Lobby, listen) chiamato."));
}

// =========================
// JOIN GAME SESSION
// =========================

void AMenuSystemCharacter::JoinGameSession()
{
	DBG_MSG(FColor::Orange, TEXT("[JOIN] JoinGameSession() chiamata. Avvio ricerca sessioni..."));

	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: OnlineSessionInterface non valida."));
		return;
	}

	if (SessionSearch.IsValid() && SessionSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		DBG_WARN(TEXT("[JOIN] ATTENZIONE: una ricerca sessioni è già in corso."));
		return;
	}

	// ---------------------------------------------------------
	// BUG FIX #1: handle salvato correttamente
	// ---------------------------------------------------------
	FindSessionsCompleteDelegateHandle =
		OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);

	DBG_MSG(FColor::Orange, TEXT("[JOIN] FindSessionsDelegate registrato."));

	SessionSearch = MakeShareable(new FOnlineSessionSearch());

	SessionSearch->MaxSearchResults = 10000;
	SessionSearch->bIsLanQuery      = false;

	SessionSearch->QuerySettings.Set(
		FName(TEXT("SEARCH_PRESENCE")),
		true,
		EOnlineComparisonOp::Equals
	);

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

	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	
	if (!LocalPlayer)
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: Nessun LocalPlayer trovato!"));
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		return;
	}

	TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId().GetUniqueNetId();

	if (!PlayerId.IsValid())
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: UniqueNetId del player non valido! Steam potrebbe non essere loggato."));
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		return;
	}

	DBG_MSG(FColor::Orange, FString::Printf(
		TEXT("[JOIN] Player NetId: %s. Avvio FindSessions (ASYNC)..."),
		*PlayerId->ToString()
	));

	bool bStarted = OnlineSessionInterface->FindSessions(
		*LocalPlayer->GetPreferredUniqueNetId(),
		SessionSearch.ToSharedRef()
	);

	if (!bStarted)
	{
		DBG_ERR(TEXT("[JOIN] ERRORE: FindSessions() ha ritornato false immediatamente!"));
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
	// BUG FIX #1 (pulizia): handle salvato, Clear funziona ora.
	// ---------------------------------------------------------
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

	if (!bWasSuccessful)
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: la ricerca sessioni è fallita."));
		return;
	}

	if (!SessionSearch.IsValid())
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: SessionSearch non è valida dopo la ricerca!"));
		return;
	}

	int32 NumResults = SessionSearch->SearchResults.Num();
	DBG_MSG(NumResults > 0 ? FColor::Green : FColor::Yellow,
		FString::Printf(TEXT("[FIND CB] Sessioni trovate: %d"), NumResults)
	);

	if (NumResults == 0)
	{
		DBG_WARN(TEXT("[FIND CB] Nessuna sessione disponibile."));
		return;
	}

	for (int32 i = 0; i < SessionSearch->SearchResults.Num(); ++i)
	{
		const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[i];

		const FString SessionId  = Result.GetSessionIdStr();
		const FString OwnerName  = Result.Session.OwningUserName;
		const int32   Ping       = Result.PingInMs;
		const int32   OpenSlots  = Result.Session.NumOpenPublicConnections;

		FString MatchType;
		Result.Session.SessionSettings.Get(FName("MatchType"), MatchType);

		DBG_MSG(FColor::Cyan, FString::Printf(
			TEXT("[FIND CB] [%d/%d] Id=%s | Host=%s | Ping=%dms | SlotsLiberi=%d | MatchType=%s"),
			i + 1, NumResults, *SessionId, *OwnerName, Ping, OpenSlots,
			MatchType.IsEmpty() ? TEXT("(non impostato)") : *MatchType
		));

		if (MatchType == TEXT("FreeForAll"))
		{
			DBG_MSG(FColor::Green, FString::Printf(
				TEXT("[FIND CB] Sessione FreeForAll trovata! (index %d) Tentativo di join..."), i
			));

			if (OpenSlots <= 0)
			{
				DBG_WARN(TEXT("[FIND CB] La sessione è piena (0 slot liberi). Cerco la prossima..."));
				continue;
			}

			// ---------------------------------------------------------
			// BUG FIX #2: handle salvato correttamente
			// ---------------------------------------------------------
			JoinSessionCompleteDelegateHandle =
				OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

			DBG_MSG(FColor::Green, TEXT("[FIND CB] JoinDelegate registrato."));

			const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

			if (!LocalPlayer)
			{
				DBG_ERR(TEXT("[FIND CB] ERRORE: LocalPlayer non trovato!"));
				OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
				return;
			}

			TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId().GetUniqueNetId();
			if (!PlayerId.IsValid())
			{
				DBG_ERR(TEXT("[FIND CB] ERRORE: UniqueNetId non valido!"));
				OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
				return;
			}

			DBG_MSG(FColor::Green, FString::Printf(
				TEXT("[FIND CB] Avvio JoinSession per l'host: %s..."), *OwnerName
			));

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

			break;
		}
		else
		{
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
	FString ResultStr;
	switch (Result)
	{
		case EOnJoinSessionCompleteResult::Success:                ResultStr = TEXT("Success");                break;
		case EOnJoinSessionCompleteResult::SessionIsFull:          ResultStr = TEXT("SessionIsFull");          break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:    ResultStr = TEXT("SessionDoesNotExist");    break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:ResultStr = TEXT("CouldNotRetrieveAddress");break;
		case EOnJoinSessionCompleteResult::AlreadyInSession:       ResultStr = TEXT("AlreadyInSession");       break;
		case EOnJoinSessionCompleteResult::UnknownError:
		default:                                                   ResultStr = TEXT("UnknownError");           break;
	}

	DBG_MSG(Result == EOnJoinSessionCompleteResult::Success ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[JOIN CB] OnJoinSessionsComplete | Sessione: %s | Risultato: %s"),
		*SessionName.ToString(), *ResultStr
	));

	// ---------------------------------------------------------
	// BUG FIX #2 (pulizia): handle salvato, Clear funziona ora.
	// ---------------------------------------------------------
	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: OnlineSessionInterface non valida durante la callback!"));
		return;
	}

	OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	DBG_MSG(FColor::Green, TEXT("[JOIN CB] JoinDelegate pulito."));

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		DBG_ERR(FString::Printf(TEXT("[JOIN CB] ERRORE nel join: %s"), *ResultStr));

		if (Result == EOnJoinSessionCompleteResult::SessionIsFull)
			{DBG_WARN(TEXT("[JOIN CB] La sessione è piena. Riprova più tardi."));}
		else if (Result == EOnJoinSessionCompleteResult::SessionDoesNotExist)
			{DBG_WARN(TEXT("[JOIN CB] La sessione non esiste più."));}
		else if (Result == EOnJoinSessionCompleteResult::AlreadyInSession)
			{DBG_WARN(TEXT("[JOIN CB] Sei già in questa sessione."));}
		else if (Result == EOnJoinSessionCompleteResult::CouldNotRetrieveAddress)
			{DBG_WARN(TEXT("[JOIN CB] Impossibile ottenere l'indirizzo. Controlla bInitServerOnClient=true nel DefaultEngine.ini!"));}

		return;
	}

	// -------------------------------------------------------
	// Join riuscito: risolvi l'indirizzo e connettiti al server.
	// In C++ il travel NON è automatico come in Blueprint:
	// bisogna chiamare GetResolvedConnectString + ClientTravel.
	// -------------------------------------------------------

	FString Address;
	if (!OnlineSessionInterface->GetResolvedConnectString(SessionName, Address))
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: GetResolvedConnectString() fallito!"));
		DBG_WARN(TEXT("[JOIN CB] Controlla che bInitServerOnClient=true sia nel DefaultEngine.ini!"));
		return;
	}

	DBG_MSG(FColor::Green, FString::Printf(
		TEXT("[JOIN CB] Indirizzo server risolto: %s. Avvio ClientTravel..."), *Address
	));

	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();

	if (!PC)
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: PlayerController non trovato!"));
		return;
	}

	PC->ClientTravel(Address, TRAVEL_Absolute);

	DBG_MSG(FColor::Green, FString::Printf(
		TEXT("[JOIN CB] ClientTravel avviato verso: %s"), *Address
	));
}

// =========================
// CALLBACK: INVITO STEAM ACCETTATO
// =========================

void AMenuSystemCharacter::OnSessionUserInviteAccepted(
	bool bWasSuccessful,
	int32 ControllerId,
	TSharedPtr<const FUniqueNetId> UserId,
	const FOnlineSessionSearchResult& InviteResult)
{
	// ---------------------------------------------------------
	// BUG FIX #6: gestione degli inviti Steam.
	//
	// Chiamata automaticamente da Steam quando l'utente accetta
	// un invito dall'overlay (Shift+Tab -> Amici -> Unisciti).
	//
	// PREREQUISITI OBBLIGATORI nel DefaultEngine.ini:
	//   [OnlineSubsystemSteam]
	//   bInitServerOnClient=true   <-- DEVE essere decommentato
	//
	// Senza bInitServerOnClient=true:
	//   - Questo delegate NON viene mai chiamato
	//   - GetResolvedConnectString() fallisce in OnJoinSessionsComplete
	//   - La connessione P2P Steam non funziona
	// ---------------------------------------------------------

	DBG_MSG(bWasSuccessful ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[INVITE] OnSessionUserInviteAccepted | Successo: %s | ControllerId: %d"),
		bWasSuccessful ? TEXT("SI") : TEXT("NO"), ControllerId
	));

	if (!bWasSuccessful)
	{
		DBG_ERR(TEXT("[INVITE] ERRORE: l'invito non è stato accettato correttamente."));
		return;
	}

	if (!InviteResult.IsValid())
	{
		DBG_ERR(TEXT("[INVITE] ERRORE: il risultato dell'invito non è valido!"));
		return;
	}

	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[INVITE] ERRORE: OnlineSessionInterface non valida!"));
		return;
	}

	if (!UserId.IsValid())
	{
		DBG_ERR(TEXT("[INVITE] ERRORE: UserId non valido!"));
		return;
	}

	DBG_MSG(FColor::Green, FString::Printf(
		TEXT("[INVITE] Invito accettato! Host: %s. Avvio JoinSession via invito..."),
		*InviteResult.Session.OwningUserName
	));

	// Registra il JoinDelegate e salva l'handle
	JoinSessionCompleteDelegateHandle =
		OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

	// Avvia il join con il risultato dell'invito (ASYNC)
	// Il risultato arriverà in OnJoinSessionsComplete -> ClientTravel
	bool bJoinStarted = OnlineSessionInterface->JoinSession(
		*UserId,
		NAME_GameSession,
		InviteResult
	);

	if (!bJoinStarted)
	{
		DBG_ERR(TEXT("[INVITE] ERRORE: JoinSession() via invito ha ritornato false immediatamente!"));
		OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	}
	else
	{
		DBG_MSG(FColor::Green, TEXT("[INVITE] JoinSession() via invito avviato. In attesa del callback..."));
	}
}

// =========================
// INPUT HANDLERS
// =========================

void AMenuSystemCharacter::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AMenuSystemCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AMenuSystemCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AMenuSystemCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AMenuSystemCharacter::DoJumpStart()
{
	Jump();
}

void AMenuSystemCharacter::DoJumpEnd()
{
	StopJumping();
}