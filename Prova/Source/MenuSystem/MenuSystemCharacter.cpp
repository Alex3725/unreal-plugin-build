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
#include "Kismet/GameplayStatics.h"

// ============================================================
// FIX #15: DEFINE_LOG_CATEGORY(LogTemplateCharacter)
// Dichiarato in MenuSystemCharacter.h con DECLARE_LOG_CATEGORY_EXTERN
// ma mai definito nel .cpp originale. Senza questa riga il linker
// può generare errori LNK2001 (simbolo esterno non risolto).
// ============================================================
DEFINE_LOG_CATEGORY(LogTemplateCharacter);

// ============================================================
// HELPER MACRO: stampa a schermo + log contemporaneamente.
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

	CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this, &AMenuSystemCharacter::OnCreateSessionComplete)),
	DestroySessionCompleteDelegate(FOnDestroySessionCompleteDelegate::CreateUObject(this, &AMenuSystemCharacter::OnDestroySessionComplete)),
	FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete)),
	JoinSessionCompleteDelegate(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionsComplete)),
	SessionUserInviteAcceptedDelegate(FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::OnSessionUserInviteAccepted))

{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate              = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->JumpZVelocity             = 500.f;
	GetCharacterMovement()->AirControl                = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed              = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed        = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength         = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	bCreateSessionOnDestroy = false;
	bJoinSessionOnDestroy   = false;

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

			SessionUserInviteAcceptedDelegateHandle =
				OnlineSessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
					SessionUserInviteAcceptedDelegate
				);

			DBG_MSG(FColor::Green, TEXT("[INIT] InviteAcceptedDelegate registrato."));
		}
		else
		{
			DBG_ERR(TEXT("[INIT] ERRORE: OnlineSessionInterface NON valida!"));
		}
	}
	else
	{
		DBG_ERR(TEXT("[INIT] ERRORE CRITICO: Nessun subsistema online trovato!"));
		DBG_WARN(TEXT("[INIT] Verifica: Steam avviato? OnlineSubsystemSteam abilitato nel .uproject?"));
	}
}

// =========================
// END PLAY
// =========================

void AMenuSystemCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// -------------------------------------------------------
	// FIX #7: rimuovi il delegate degli inviti Steam.
	// Senza questa pulizia, al cambio di livello (Main Menu->Lobby)
	// l'handle rimane valido nell'OnlineSessionInterface ma punta
	// a un attore distrutto (dangling handle).
	// -------------------------------------------------------
	if (OnlineSessionInterface.IsValid() && SessionUserInviteAcceptedDelegateHandle.IsValid())
	{
		OnlineSessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(
			SessionUserInviteAcceptedDelegateHandle
		);
		DBG_MSG(FColor::White, TEXT("[CLEANUP] InviteAcceptedDelegate rimosso (EndPlay)."));
	}

	// -------------------------------------------------------
	// FIX #10: cancella il timer del delay prima di OpenLevel.
	// Senza questa pulizia, se l'attore venisse distrutto per
	// qualsiasi motivo nei 0.5s di attesa, la lambda [this]
	// verrebbe eseguita su un puntatore non più valido.
	// -------------------------------------------------------
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LobbyTravelTimerHandle);
	}
}

// =========================
// INPUT
// =========================

void AMenuSystemCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(JumpAction,      ETriggerEvent::Started,   this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction,      ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		EnhancedInputComponent->BindAction(MoveAction,      ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Look);
		EnhancedInputComponent->BindAction(LookAction,      ETriggerEvent::Triggered, this, &AMenuSystemCharacter::Look);
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
		DBG_WARN(TEXT("[CREATE] Sessione esistente trovata. Distruggo prima di crearne una nuova..."));
		bCreateSessionOnDestroy = true;

		DestroySessionCompleteDelegateHandle =
			OnlineSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegate);

		OnlineSessionInterface->DestroySession(NAME_GameSession);
		return;
	}

	CreateSessionCompleteDelegateHandle =
		OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegate);

	DBG_MSG(FColor::Cyan, TEXT("[CREATE] Delegate registrato. Configuro impostazioni sessione..."));

	TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShared<FOnlineSessionSettings>();

	SessionSettings->bIsLANMatch            = false;
	SessionSettings->NumPublicConnections   = 4;
	SessionSettings->bShouldAdvertise       = true;
	SessionSettings->bUsesPresence          = true;
	SessionSettings->bAllowJoinInProgress   = true;
	SessionSettings->bAllowJoinViaPresence  = true;
	SessionSettings->bUseLobbiesIfAvailable = true;
	SessionSettings->bAllowInvites          = true;
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
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		return;
	}

	// -------------------------------------------------------
	// FIX #12: usa GetPreferredUniqueNetId() invece di GetControllerId().
	// GetControllerId() è legacy in UE5. Steam richiede l'identificatore
	// univoco del player per associare correttamente la lobby all'account.
	// -------------------------------------------------------
	const FUniqueNetIdRepl& PlayerNetId = LocalPlayer->GetPreferredUniqueNetId();
	if (!PlayerNetId.IsValid())
	{
		DBG_ERR(TEXT("[CREATE] ERRORE: UniqueNetId non valido! Steam potrebbe non essere loggato."));
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		return;
	}

	DBG_MSG(FColor::Cyan, FString::Printf(
		TEXT("[CREATE] Player NetId: %s. Avvio CreateSession..."),
		*PlayerNetId->ToString()
	));

	bool bStarted = OnlineSessionInterface->CreateSession(
		*PlayerNetId,
		NAME_GameSession,
		*SessionSettings
	);

	if (!bStarted)
	{
		DBG_ERR(TEXT("[CREATE] ERRORE: CreateSession() ha ritornato false!"));
		DBG_WARN(TEXT("[CREATE] Controlla: Steam avviato? SteamDevAppId=480? bEnabled=true?"));
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	}
	else
	{
		DBG_MSG(FColor::Cyan, TEXT("[CREATE] CreateSession() avviato. In attesa del callback..."));
	}
}

// =========================
// CALLBACK: DESTROY SESSION COMPLETE
// =========================

void AMenuSystemCharacter::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
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
			DBG_MSG(FColor::Cyan, TEXT("[DESTROY] Riciclo: richiamo CreateGameSession()..."));
			bCreateSessionOnDestroy = false;
			CreateGameSession();
		}
		else if (bJoinSessionOnDestroy)
		{
			// -------------------------------------------------------
			// FIX #8: flusso invito Steam con sessione preesistente.
			// La sessione è stata distrutta, ora possiamo fare il join.
			// -------------------------------------------------------

			// FIX #12 (md): controlla validità prima di usare PendingInviteResult
			if (!PendingInviteResult.IsValid())
			{
				DBG_ERR(TEXT("[DESTROY] ERRORE: PendingInviteResult non valido! Invito scaduto o non più disponibile."));
				bJoinSessionOnDestroy = false;
				return;
			}

			DBG_MSG(FColor::Green, TEXT("[DESTROY] Sessione distrutta. Eseguo join all'invito in attesa..."));
			bJoinSessionOnDestroy = false;
			InternalJoinSession(PendingInviteResult);
		}
		else
		{
			DBG_MSG(FColor::White, TEXT("[DESTROY] Sessione distrutta. Nessuna azione successiva."));
		}
	}
	else
	{
		DBG_ERR(TEXT("[DESTROY] ERRORE: la distruzione della sessione è fallita!"));
		bCreateSessionOnDestroy = false;
		bJoinSessionOnDestroy   = false;
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
		DBG_WARN(TEXT("[CREATE CB] Controlla: 1) Steam avviato? 2) SteamDevAppId=480? 3) bEnabled=true?"));
		return;
	}

	// -------------------------------------------------------
	// FIX #10: DELAY prima di OpenLevel.
	//
	// PROBLEMA:
	//   Steam registra la lobby in modo ASINCRONO. Il callback
	//   OnCreateSessionComplete(success=true) arriva spesso PRIMA
	//   che la lobby sia realmente visibile online.
	//   Se OpenLevel viene chiamato subito:
	//     - la sessione risulta invisibile a FindSessions
	//     - il join fallisce con CouldNotRetrieveAddress
	//     - ClientTravel non connette
	//
	// SOLUZIONE:
	//   Timer di 0.5s prima di aprire il listen server.
	//   Questo dà a Steam il tempo di completare la registrazione.
	//
	// NOTA: LobbyTravelTimerHandle viene pulito in EndPlay()
	//       per evitare la callback lambda su attore distrutto.
	// -------------------------------------------------------

	DBG_MSG(FColor::Cyan,
		TEXT("[CREATE CB] Sessione creata! Attendo 0.5s (Steam lobby registration) prima di OpenLevel..."));

	GetWorld()->GetTimerManager().SetTimer(
		LobbyTravelTimerHandle,
		[this]()
		{
			if (!IsValid(this))
			{
				return;
			}

			DBG_MSG(FColor::Cyan, TEXT("[CREATE CB] Avvio OpenLevel verso Lobby come listen server..."));

			GetWorld()->ServerTravel("/Game/ThirdPerson/Maps/Lobby?listen");

			DBG_MSG(FColor::Cyan, TEXT("[CREATE CB] OpenLevel(Lobby, listen) chiamato."));
		},
		0.5f,
		false
	);
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
		DBG_WARN(TEXT("[JOIN] Una ricerca è già in corso."));
		return;
	}

	FindSessionsCompleteDelegateHandle =
		OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);

	DBG_MSG(FColor::Orange, TEXT("[JOIN] FindSessionsDelegate registrato."));

	SessionSearch = MakeShareable(new FOnlineSessionSearch());

	// FIX #13: 100 invece di 10000 — Steam limita internamente comunque
	SessionSearch->MaxSearchResults = 100;
	SessionSearch->bIsLanQuery      = false;

	// -------------------------------------------------------
	// FIX #11: rimosso SEARCH_PRESENCE.
	// In UE5.3+ con bUseLobbiesIfAvailable=true, Steam usa
	// il sistema Lobby. SEARCH_PRESENCE e SEARCH_LOBBIES insieme
	// possono creare filtri in conflitto che rendono le sessioni
	// invisibili o non trovabili. Si usa solo SEARCH_LOBBIES.
	// -------------------------------------------------------
	SessionSearch->QuerySettings.Set(
		SEARCH_LOBBIES,
		true,
		EOnlineComparisonOp::Equals
	);

	DBG_MSG(FColor::Orange, FString::Printf(
		TEXT("[JOIN] Parametri ricerca: MaxResults=%d | LAN=%s | Lobbies=true"),
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
		DBG_ERR(TEXT("[JOIN] ERRORE: UniqueNetId non valido! Steam potrebbe non essere loggato."));
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
		DBG_ERR(TEXT("[JOIN] ERRORE: FindSessions() ha ritornato false!"));
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}
	else
	{
		DBG_MSG(FColor::Orange, TEXT("[JOIN] FindSessions() avviato. In attesa callback OnFindSessionsComplete..."));
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

	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		DBG_MSG(FColor::Green, TEXT("[FIND CB] FindDelegate pulito."));
	}
	else
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: OnlineSessionInterface non valida!"));

		// FIX #14: reset anche in caso di errore
		SessionSearch.Reset();
		return;
	}

	if (!bWasSuccessful)
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: la ricerca sessioni è fallita."));
		SessionSearch.Reset(); // FIX #14
		return;
	}

	if (!SessionSearch.IsValid())
	{
		DBG_ERR(TEXT("[FIND CB] ERRORE: SessionSearch non valida dopo la ricerca!"));
		return;
	}

	int32 NumResults = SessionSearch->SearchResults.Num();
	DBG_MSG(NumResults > 0 ? FColor::Green : FColor::Yellow,
		FString::Printf(TEXT("[FIND CB] Sessioni trovate: %d"), NumResults)
	);

	if (NumResults == 0)
	{
		DBG_WARN(TEXT("[FIND CB] Nessuna sessione disponibile."));
		SessionSearch.Reset(); // FIX #14
		return;
	}

	bool bJoinAttempted = false;

	for (int32 i = 0; i < SessionSearch->SearchResults.Num(); ++i)
	{
		const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[i];

		const FString SessionId = Result.GetSessionIdStr();
		const FString OwnerName = Result.Session.OwningUserName;
		const int32   Ping      = Result.PingInMs;
		const int32   OpenSlots = Result.Session.NumOpenPublicConnections;

		FString MatchType;
		Result.Session.SessionSettings.Get(FName("MatchType"), MatchType);

		DBG_MSG(FColor::Cyan, FString::Printf(
			TEXT("[FIND CB] [%d/%d] Id=%s | Host=%s | Ping=%dms | Slots=%d | MatchType=%s"),
			i + 1, NumResults, *SessionId, *OwnerName, Ping, OpenSlots,
			MatchType.IsEmpty() ? TEXT("(non impostato)") : *MatchType
		));

		if (MatchType == TEXT("FreeForAll"))
		{
			if (OpenSlots <= 0)
			{
				DBG_WARN(TEXT("[FIND CB] Sessione piena (0 slot liberi). Cerco la prossima..."));
				continue;
			}

			DBG_MSG(FColor::Green, FString::Printf(
				TEXT("[FIND CB] Sessione FreeForAll trovata (index %d). Avvio join..."), i
			));

			InternalJoinSession(Result);
			bJoinAttempted = true;
			break;
		}
		else
		{
			DBG_MSG(FColor::White, FString::Printf(
				TEXT("[FIND CB] [%d] Ignorata: MatchType='%s' (atteso 'FreeForAll')"),
				i, *MatchType
			));
		}
	}

	if (!bJoinAttempted)
	{
		DBG_WARN(TEXT("[FIND CB] Nessuna sessione FreeForAll disponibile tra i risultati."));
	}

	// -------------------------------------------------------
	// FIX #14: Reset di SessionSearch dopo l'elaborazione.
	// Tenere viva la TSharedPtr dopo il find può causare:
	//   - risultati stale alla ricerca successiva
	//   - stato di ricerca bloccato su "InProgress"
	//   - doppi callback in casi edge
	// -------------------------------------------------------
	SessionSearch.Reset();
}

// =========================
// INTERNAL JOIN SESSION
// =========================

void AMenuSystemCharacter::InternalJoinSession(const FOnlineSessionSearchResult& SessionResult)
{
	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[INTERNAL JOIN] ERRORE: OnlineSessionInterface non valida!"));
		return;
	}

	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (!LocalPlayer)
	{
		DBG_ERR(TEXT("[INTERNAL JOIN] ERRORE: LocalPlayer non trovato!"));
		return;
	}

	TSharedPtr<const FUniqueNetId> PlayerId = LocalPlayer->GetPreferredUniqueNetId().GetUniqueNetId();
	if (!PlayerId.IsValid())
	{
		DBG_ERR(TEXT("[INTERNAL JOIN] ERRORE: UniqueNetId non valido!"));
		return;
	}

	JoinSessionCompleteDelegateHandle =
		OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

	DBG_MSG(FColor::Green, FString::Printf(
		TEXT("[INTERNAL JOIN] Avvio JoinSession per l'host: %s..."),
		*SessionResult.Session.OwningUserName
	));

	bool bJoinStarted = OnlineSessionInterface->JoinSession(
		*LocalPlayer->GetPreferredUniqueNetId(),
		NAME_GameSession,
		SessionResult
	);

	if (!bJoinStarted)
	{
		DBG_ERR(TEXT("[INTERNAL JOIN] ERRORE: JoinSession() ha ritornato false!"));
		OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	}
	else
	{
		DBG_MSG(FColor::Green, TEXT("[INTERNAL JOIN] JoinSession() avviato. In attesa callback OnJoinSessionsComplete..."));
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
		case EOnJoinSessionCompleteResult::Success:                 ResultStr = TEXT("Success");                 break;
		case EOnJoinSessionCompleteResult::SessionIsFull:           ResultStr = TEXT("SessionIsFull");           break;
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:     ResultStr = TEXT("SessionDoesNotExist");     break;
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress: ResultStr = TEXT("CouldNotRetrieveAddress"); break;
		case EOnJoinSessionCompleteResult::AlreadyInSession:        ResultStr = TEXT("AlreadyInSession");        break;
		case EOnJoinSessionCompleteResult::UnknownError:
		default:                                                    ResultStr = TEXT("UnknownError");            break;
	}

	DBG_MSG(Result == EOnJoinSessionCompleteResult::Success ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[JOIN CB] OnJoinSessionsComplete | Sessione: %s | Risultato: %s"),
		*SessionName.ToString(), *ResultStr
	));

	if (!OnlineSessionInterface.IsValid())
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: OnlineSessionInterface non valida!"));
		return;
	}

	OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	DBG_MSG(FColor::Green, TEXT("[JOIN CB] JoinDelegate pulito."));

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		DBG_ERR(FString::Printf(TEXT("[JOIN CB] ERRORE nel join: %s"), *ResultStr));

		if      (Result == EOnJoinSessionCompleteResult::SessionIsFull)
			{DBG_WARN(TEXT("[JOIN CB] La sessione è piena."));}
		else if (Result == EOnJoinSessionCompleteResult::SessionDoesNotExist)
			{DBG_WARN(TEXT("[JOIN CB] La sessione non esiste più."));}
		else if (Result == EOnJoinSessionCompleteResult::AlreadyInSession)
			{DBG_WARN(TEXT("[JOIN CB] Sei già in questa sessione."));}
		else if (Result == EOnJoinSessionCompleteResult::CouldNotRetrieveAddress)
			{DBG_WARN(TEXT("[JOIN CB] Impossibile ottenere l'indirizzo. Controlla bInitServerOnClient=true e !NetDriverDefinitions in DefaultEngine.ini!"));}

		return;
	}

	FString Address;
	if (!OnlineSessionInterface->GetResolvedConnectString(SessionName, Address))
	{
		DBG_ERR(TEXT("[JOIN CB] ERRORE: GetResolvedConnectString() fallito!"));
		DBG_WARN(TEXT("[JOIN CB] Verifica: bInitServerOnClient=true + !NetDriverDefinitions=ClearArray in DefaultEngine.ini"));
		return;
	}

	DBG_MSG(FColor::Green, FString::Printf(
		TEXT("[JOIN CB] Indirizzo risolto: %s. Avvio ClientTravel..."), *Address
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
	DBG_MSG(bWasSuccessful ? FColor::Green : FColor::Red,
		FString::Printf(TEXT("[INVITE] OnSessionUserInviteAccepted | Successo: %s | ControllerId: %d"),
		bWasSuccessful ? TEXT("SI") : TEXT("NO"), ControllerId
	));

	if (!bWasSuccessful)
	{
		DBG_ERR(TEXT("[INVITE] ERRORE: l'invito non è stato accettato."));
		return;
	}

	if (!InviteResult.IsValid())
	{
		DBG_ERR(TEXT("[INVITE] ERRORE: risultato dell'invito non valido!"));
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
		TEXT("[INVITE] Invito accettato! Host: %s."),
		*InviteResult.Session.OwningUserName
	));

	// -------------------------------------------------------
	// FIX #8: controlla se siamo già in una sessione.
	// Se sì, la distruggiamo prima di fare il join per evitare
	// AlreadyInSession o CouldNotRetrieveAddress.
	// Il join avverrà in OnDestroySessionComplete() via
	// bJoinSessionOnDestroy + PendingInviteResult.
	// -------------------------------------------------------
	auto ExistingSession = OnlineSessionInterface->GetNamedSession(NAME_GameSession);
	if (ExistingSession != nullptr)
	{
		DBG_WARN(TEXT("[INVITE] Sessione attiva rilevata. Distruggo prima di accettare l'invito..."));

		PendingInviteResult   = InviteResult;
		bJoinSessionOnDestroy = true;

		DestroySessionCompleteDelegateHandle =
			OnlineSessionInterface->AddOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegate);

		OnlineSessionInterface->DestroySession(NAME_GameSession);
		return;
	}

	DBG_MSG(FColor::Green, TEXT("[INVITE] Nessuna sessione attiva. Join diretto..."));
	InternalJoinSession(InviteResult);
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
		const FRotator Rotation    = GetController()->GetControlRotation();
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