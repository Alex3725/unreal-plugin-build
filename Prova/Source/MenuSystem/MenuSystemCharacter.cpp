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



AMenuSystemCharacter::AMenuSystemCharacter(): 
	CreateSessionCompleteDelegate(FOnCreateSessionCompleteDelegate::CreateUObject(this,&AMenuSystemCharacter::OnCreateSessionComplete)),
	//Quando la creazione della sessione multiplayer è finita, chiama questa funzione
	//Quando CreateSession finisce, chiama OncCreateSessionComplete dentro questo Character

	FindSessionsCompleteDelegate(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete)),

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
	
	// =========================
	// ONLINE SUBSYSTEM INIT
	// =========================

	IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get();

	if (Subsystem)
	{
		OnlineSessionInterface = Subsystem->GetSessionInterface();
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				5.f,
				FColor::Green,
				FString::Printf(TEXT("Found subsystem %s"),
				*Subsystem->GetSubsystemName().ToString())
			);
		}
	}
}


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
		UE_LOG(LogMenuSystem, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

// =========================
// CREATE SESSION
// =========================

void AMenuSystemCharacter::CreateGameSession()
{
	if (!OnlineSessionInterface.IsValid())
	{
		return;
	}

	// Se esiste già una sessione, distruggila prima
	auto ExistingSession = OnlineSessionInterface->GetNamedSession(NAME_GameSession);

	if (ExistingSession != nullptr)
	{
		OnlineSessionInterface->DestroySession(NAME_GameSession);
		return; // IMPORTANTE: attendere callback DestroySession in progetto serio
	}

	// Bind delegate
	CreateSessionCompleteDelegateHandle =
		OnlineSessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
			CreateSessionCompleteDelegate);

	// Session settings
	TSharedPtr<FOnlineSessionSettings> SessionSettings = MakeShared<FOnlineSessionSettings>();

	SessionSettings->bIsLANMatch = false; 
	SessionSettings->NumPublicConnections = 4;
	SessionSettings->bShouldAdvertise = true;
	SessionSettings->bUsesPresence = true;
	SessionSettings->bAllowJoinInProgress = true;
	SessionSettings->bAllowJoinViaPresence = true;
	SessionSettings->bUseLobbiesIfAvailable = true;
	SessionSettings->bAllowInvites = true;
	SessionSettings->bAllowJoinViaPresenceFriendsOnly = false;

	//SessionSettings->bShouldAdvertise = false; // per non apparire server pubblici

	SessionSettings->Set(FName("MatchType"),FString("FreeForAll"), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	//“Questa sessione ha un’etichetta chiamata MatchType che vale FreeForAll e deve essere visibile agli altri giocatori quando cercano server.”
	// FString("FreeForAll") Che tipo di partita è questo server?
	//FName("MatchType") e la key es MatchType = FreeForAll
	

	// Get player
	ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

	if (!LocalPlayer)
	{
		return;
	}

	// Create session
	OnlineSessionInterface->CreateSession(
		LocalPlayer->GetControllerId(),
		NAME_GameSession,
		*SessionSettings
	);
}

void AMenuSystemCharacter::JoinGameSession()
{
	// find game sessio
	
	// 🎮 STEP 1: controlla se il sistema online è valido
	if (!OnlineSessionInterface.IsValid())
	{
		return;
	}
	
	// 🔗 STEP 2: registra il delegate
	// Quando FindSessions finisce, Unreal chiamerà OnFindSessionsComplete
	
	OnlineSessionInterface->AddOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegate);
	//						questa = fuzione che appena finisce find session manda questa FindSessionsCompleteDelegate che e nel costruttore che manda 
	// FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete) = Crea l’oggetto che contiene i risultati della ricerca
	
	// 🔍 STEP 3: crea oggetto ricerca sessioni
	SessionSearch = MakeShareable(new FOnlineSessionSearch());
	
	// ⚙️ STEP 4: configurazione ricerca
	SessionSearch->MaxSearchResults = 10000;   // tante sessioni possibili (Steam dev)
	SessionSearch->bIsLanQuery = false;        // online, non LAN
	
	
	SessionSearch->QuerySettings.Set(FName(TEXT("SEARCH_PRESENCE")),true,EOnlineComparisonOp::Equals);
	//SessionSearch->QuerySettings È un contenitore di filtri di ricerca.
	//Set È una funzione che aggiunge una regola di filtro.
	//FName(TEXT("SEARCH_PRESENCE") “stiamo filtrando per presenza online”
	//true voglio SOLO sessioni con presenza attiva
	//EOnlineComparisonOp::Equals deve essere ESATTAMENTE uguale a true
	// 🔥 AGGIUNGI QUESTA RIGA
	SessionSearch->QuerySettings.Set(
		SEARCH_LOBBIES,
		true,
		EOnlineComparisonOp::Equals
	);
	
	// 👤 STEP 5: prendi il player locale
	const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	
	if (!LocalPlayer)
	{
		return;
	}
	
	// 🚀 STEP 6: avvia ricerca sessioni (ASYNC)
	OnlineSessionInterface->FindSessions(
		*LocalPlayer->GetPreferredUniqueNetId(),  // ID player
		SessionSearch.ToSharedRef()               // dove salvare risultati
	);
	

	// ⏳ da qui in poi Unreal lavora in background
	// 👉 quando finisce chiama OnFindSessionsComplete
	
}

void AMenuSystemCharacter::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (!GEngine)
	{
		return;
	}
	FString Message = FString::Printf(
	TEXT("Session Create: %s | Name: %s"),
	bWasSuccessful ? TEXT("SUCCESS") : TEXT("FAIL"),
	*SessionName.ToString()
);

	FColor Color = bWasSuccessful ? FColor::Cyan : FColor::Red;

	GEngine->AddOnScreenDebugMessage(-1, 5.f, Color, Message);
	
	if (!bWasSuccessful) return;
	
	UWorld* World = GetWorld();

	if (World)
	{
		World->ServerTravel(FString("/Game/ThirdPerson/Maps/Lobby?listen"));
		//C:\Users\PcAle\Documents\UnrealProject\MenuSystem\Content\ = /Game/
		//?listen = lo apre come listen server 
	}
	
	
	// pulizia delegate (buona pratica)
	if (OnlineSessionInterface.IsValid())
	{
		OnlineSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	}
}

// =========================
// CALLBACK SESSION CREATE
// =========================

void AMenuSystemCharacter::OnFindSessionsComplete(bool bWasSuccessful)
{
	// 🔒 sicurezza: Engine deve esistere per stampare a schermo
	if (!GEngine)
	{
		return;
	}
	if (!OnlineSessionInterface.IsValid())
	{
		return;
	}

	// ❌ caso: la ricerca sessioni è fallita
	if (!bWasSuccessful)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.f,
			FColor::Red,
			TEXT("✖ FIND SESSIONS FALLITO")
		);
		return;
	}

	// ❌ caso: SessionSearch non valido o nessuna sessione trovata
	if (!SessionSearch.IsValid() || SessionSearch->SearchResults.Num() == 0)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.f,
			FColor::Yellow,
			TEXT("⚠ NESSUNA SESSIONE TROVATA")
		);
		return;
	}

	// ✔ caso positivo: almeno una sessione trovata
	GEngine->AddOnScreenDebugMessage(
		-1,
		3.f,
		FColor::Green,
		TEXT("✔ SESSIONI TROVATE")
	);

	// 🔁 ciclo su tutte le sessioni trovate
	for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
		// utilizzo & cosi non stai copiando l’oggetto ma stai usando un alias dell’oggetto originale nella lista
			// quindi Result punta direttamente all’elemento dentro SearchResults
	{
		// 🆔 ID univoco della sessione (utile per debug o join)
		const FString Id = Result.GetSessionIdStr();

		// 👤 nome del player host della sessione
		const FString User = Result.Session.OwningUserName;
		
		FString MatchType;
		Result.Session.SessionSettings.Get(FName("MatchType"),MatchType);

		// 🧠 costruzione stringa di debug completa
		const FString Msg = FString::Printf(
			TEXT("Id: %s | User: %s | Status: %s"),
			*Id,
			*User,
			bWasSuccessful ? TEXT("OK") : TEXT("FAIL") // ternario per stato
		);

		// 📺 stampa a schermo della sessione trovata
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.f,
			bWasSuccessful ? FColor::Cyan : FColor::Red // colore dinamico con ternario
		, Msg);
		
		if (MatchType == "FreeForAll")
		{
			GEngine->AddOnScreenDebugMessage(
				-1,
				5.f,
				FColor::Cyan,
				FString::Printf(TEXT("Joining Match Type: %s"), *MatchType));

			OnlineSessionInterface->AddOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegate);

			const ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();

			if (LocalPlayer)
			{
				OnlineSessionInterface->JoinSession(
					*LocalPlayer->GetPreferredUniqueNetId(),
					NAME_GameSession,
					Result
				);
			}

			break; // 🔥 IMPORTANTISSIMO
		}

	}
	
}

void AMenuSystemCharacter::OnJoinSessionsComplete(
	FName SessionName,
	EOnJoinSessionCompleteResult::Type Result)
{
	if (!OnlineSessionInterface.IsValid()) return;

	OnlineSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		UE_LOG(LogTemp, Error, TEXT("Join session failed"));
		return;
	}

	FString Address;
	if (!OnlineSessionInterface->GetResolvedConnectString(SessionName, Address))
	{
		UE_LOG(LogTemp, Error, TEXT("Could not get connect string"));
		return;
	}

	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();

	if (PC)
	{
		PC->ClientTravel(Address, TRAVEL_Absolute);
	}
}


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
