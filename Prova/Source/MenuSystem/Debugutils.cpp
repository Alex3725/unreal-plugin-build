// Copyright Epic Games, Inc. All Rights Reserved.

#include "DebugUtils.h"

// ============================================================
//  DEFINIZIONE CATEGORIA LOG
//  Va messa UNA SOLA VOLTA in tutto il progetto (nel .cpp).
//  La dichiarazione (DECLARE_LOG_CATEGORY_EXTERN) è nell'header.
// ============================================================

/**
 * Definisce la categoria log LogMenuSystemOnline.
 * - Primo parametro:  nome della categoria
 * - Secondo parametro: verbosity di default a runtime (Log = mostra Log/Warning/Error)
 * - Terzo parametro:  verbosity massima compilata (All = compila tutto)
 */
DEFINE_LOG_CATEGORY(LogMenuSystemOnline);


// ============================================================
//  IMPLEMENTAZIONE: PrintImpl (privato, usato internamente)
// ============================================================

/**
 * @brief Nucleo di tutti i metodi debug.
 *        Scrive su:
 *          1. UE_LOG    (Output Log + file di log su disco)
 *          2. Schermo   (AddOnScreenDebugMessage, solo fuori Shipping)
 *
 * @note  I messaggi a schermo vengono compilati solo se NON siamo in
 *        UE_BUILD_SHIPPING, evitando overhead e testo visibile all'utente finale.
 *        UE_LOG è sempre attivo in tutte le configurazioni (Debug, Development, Shipping)
 *        perché è utile per diagnosticare crash report da build di produzione.
 */
void FDebugUtils::PrintImpl(
	const FString&      Msg,
	FColor              Color,
	float               Duration,
	ELogVerbosity::Type Verbosity)
{
	// -------------------------------------------------------
	// 1. SCRITTURA SU UE_LOG
	//    Sempre attivo, anche in Shipping.
	//    I log vengono scritti su:
	//      - Output Log panel dell'editor
	//      - File: Saved/Logs/NomeProgetto.log
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
	// 2. MESSAGGIO A SCHERMO
	//    Compilato solo in configurazioni non-Shipping.
	//    In Shipping questo blocco viene rimosso dal compilatore
	//    (zero overhead a runtime in produzione).
	// -------------------------------------------------------
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		// -1 come Key = ogni chiamata crea un messaggio separato
		// (non sovrascrive messaggi precedenti)
		GEngine->AddOnScreenDebugMessage(
			-1,
			Duration,
			Color,
			Msg
		);
	}
#endif // !UE_BUILD_SHIPPING
}


// ============================================================
//  IMPLEMENTAZIONE: metodi pubblici
// ============================================================

/**
 * @brief Messaggio generico con colore e durata personalizzabili.
 *        Chiama PrintImpl con verbosity Log.
 */
void FDebugUtils::Log(const FString& Msg, FColor Color, float Duration)
{
	PrintImpl(Msg, Color, Duration, ELogVerbosity::Log);
}

/**
 * @brief Messaggio informativo neutro (ciano/bianco).
 *        Usato per stati, valori correnti, inizializzazioni.
 */
void FDebugUtils::Info(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Cyan, Duration, ELogVerbosity::Log);
}

/**
 * @brief Messaggio di successo (verde).
 *        Usato per confermare operazioni completate correttamente.
 */
void FDebugUtils::Success(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Green, Duration, ELogVerbosity::Log);
}

/**
 * @brief Messaggio di avvertimento (giallo).
 *        Usato per situazioni anomale ma recuperabili.
 *        Mappa su UE_LOG Warning per essere filtrabili.
 */
void FDebugUtils::Warning(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Yellow, Duration, ELogVerbosity::Warning);
}

/**
 * @brief Messaggio di errore critico (rosso).
 *        Usato per errori che bloccano il flusso normale.
 *        Mappa su UE_LOG Error per essere filtrabili.
 *        Durata a schermo più lunga (10s) per non perderli.
 */
void FDebugUtils::Error(const FString& Msg, float Duration)
{
	PrintImpl(Msg, FColor::Red, Duration, ELogVerbosity::Error);
}

/**
 * @brief Stampa un separatore con titolo di sezione nel log.
 *        Utile per raggruppare visivamente i log nel Output Log.
 *
 *        Esempio output nel log:
 *          ===== [ CREATE SESSION ] =====
 */
void FDebugUtils::Section(const FString& Title)
{
	// Costruisce la stringa separatrice con il titolo centrato
	const FString Line = FString::Printf(TEXT("===== [ %s ] ====="), *Title.ToUpper());

	// Usa sempre Log (non Warning/Error) perché è solo una etichetta visiva
	UE_LOG(LogMenuSystemOnline, Log, TEXT("%s"), *Line);

#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Purple, Line);
	}
#endif // !UE_BUILD_SHIPPING
}

/**
 * @brief Stampa una linea separatrice orizzontale senza titolo.
 *
 *        Esempio output nel log:
 *          --------------------------------------------------
 */
void FDebugUtils::Separator()
{
	const FString Line(TEXT("--------------------------------------------------"));
	UE_LOG(LogMenuSystemOnline, Log, TEXT("%s"), *Line);

	// Nessun messaggio a schermo per il semplice separatore:
	// sarebbe rumore visivo inutile durante il gioco.
}