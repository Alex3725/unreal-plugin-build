// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugUtils.h"

// ============================================================
//  DEFINIZIONE CATEGORIA LOG
//  Va definita UNA SOLA VOLTA in tutto il progetto (nel .cpp).
//  La dichiarazione (DECLARE_LOG_CATEGORY_EXTERN) è nell'header.
// ============================================================

/**
 * Definisce la categoria log LogMenuSystemOnline.
 * - Secondo parametro: verbosity di default a runtime
 * - Terzo parametro:   verbosity massima compilata
 */
DEFINE_LOG_CATEGORY(LogMenuSystemOnline);


// ============================================================
//  PrintImpl — nucleo condiviso di tutti i metodi pubblici
// ============================================================

/**
 * @brief Scrive il messaggio su:
 *   1. UE_LOG (sempre attivo, anche in Shipping)
 *   2. AddOnScreenDebugMessage (solo fuori Shipping)
 *
 * @param Key  -1 = crea sempre un nuovo messaggio a schermo
 *             >= 0 = sovrascrive lo slot con quel key (comportamento "stato")
 */
void FDebugUtils::PrintImpl(
	const FString&      Msg,
	FColor              Color,
	float               Duration,
	ELogVerbosity::Type Verbosity,
	int32               Key)
{
	// -------------------------------------------------------
	// 1. UE_LOG — sempre attivo in tutte le configurazioni
	//    Scritto su: Output Log panel + Saved/Logs/NomeProgetto.log
	// -------------------------------------------------------
	switch (Verbosity)
	{
		case ELogVerbosity::Warning:
			UE_LOG(LogMenuSystemOnline, Warning, TEXT("%s"), *Msg);
			break;
		case ELogVerbosity::Error:
			UE_LOG(LogMenuSystemOnline, Error, TEXT("%s"), *Msg);
			break;
		case ELogVerbosity::Log:
		default:
			UE_LOG(LogMenuSystemOnline, Log, TEXT("%s"), *Msg);
			break;
	}

	// -------------------------------------------------------
	// 2. Messaggio a schermo — solo fuori da UE_BUILD_SHIPPING
	//    In Shipping il compilatore rimuove questo blocco completamente.
	// -------------------------------------------------------
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Duration, Color, Msg);
	}
#endif // !UE_BUILD_SHIPPING
}


// ============================================================
//  METODI PUBBLICI
// ============================================================

void FDebugUtils::Log(const FString& Msg, FColor Color, float Duration)
{
	PrintImpl(Msg, Color, Duration, ELogVerbosity::Log, -1);
}

void FDebugUtils::Info(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Cyan, Duration, ELogVerbosity::Log, -1);
}

void FDebugUtils::Success(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Green, Duration, ELogVerbosity::Log, -1);
}

void FDebugUtils::Warning(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Yellow, Duration, ELogVerbosity::Warning, -1);
}

void FDebugUtils::Error(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Red, Duration, ELogVerbosity::Error, -1);
}

/**
 * @brief Messaggio di stato PERSISTENTE con Key fisso.
 *
 * Differenza rispetto agli altri metodi:
 *   - Key fisso (default: 200): sovrascrive il messaggio precedente nello stesso slot
 *   - Durata più lunga (20s default) per essere visibile durante il caricamento
 *   - Colore arancione per distinguersi dagli altri messaggi
 *
 * Uso tipico:
 *   FDebugUtils::Status(TEXT("🔍 CERCANDO SESSIONI..."));    // stato 1
 *   FDebugUtils::Status(TEXT("⚡ CONNESSIONE IN CORSO..."));  // stato 2 (sostituisce 1)
 *   FDebugUtils::Status(TEXT("🚀 VIAGGIO VERSO LOBBY..."));   // stato 3 (sostituisce 2)
 *   FDebugUtils::ClearStatus();                               // fine
 */
void FDebugUtils::Status(const FString& Msg, int32 Key, float Duration)
{
	// Log sempre su UE_LOG per tracciabilità
	UE_LOG(LogMenuSystemOnline, Log, TEXT("[STATUS] %s"), *Msg);

#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		// Usa Key fisso: sovrascrive il messaggio precedente nello stesso slot
		GEngine->AddOnScreenDebugMessage(Key, Duration, FColor::Orange, Msg);
	}
#endif // !UE_BUILD_SHIPPING
}

/**
 * @brief Cancella il messaggio di stato dalla schermata.
 *        Chiama questo quando il flusso è terminato (join completato, errore, timeout).
 */
void FDebugUtils::ClearStatus(int32 Key)
{
	UE_LOG(LogMenuSystemOnline, Log, TEXT("[STATUS] Cleared (Key=%d)"), Key);

#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		// Sovrascrive il messaggio con stringa vuota e durata minima
		// (non esiste una API "remove by key" diretta in UE5)
		GEngine->AddOnScreenDebugMessage(Key, 0.001f, FColor::Black, TEXT(""));
	}
#endif // !UE_BUILD_SHIPPING
}

/**
 * @brief Separatore visivo con titolo di sezione.
 *        Esempio output nel log: ===== [ CREATE SESSION ] =====
 */
void FDebugUtils::Section(const FString& Title)
{
	const FString Line = FString::Printf(TEXT("===== [ %s ] ====="), *Title.ToUpper());
	UE_LOG(LogMenuSystemOnline, Log, TEXT("%s"), *Line);

#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Purple, Line);
	}
#endif
}

/**
 * @brief Linea separatrice orizzontale senza titolo.
 *        Esempio output nel log: --------------------------------------------------
 */
void FDebugUtils::Separator()
{
	const FString Line(TEXT("--------------------------------------------------"));
	UE_LOG(LogMenuSystemOnline, Log, TEXT("%s"), *Line);
	// Nessun messaggio a schermo: sarebbe rumore visivo inutile.
}